#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#pragma clang diagnostic pop

#include "game.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define SYNTH_SAMPLE_RATE 48000U
#define SYNTH_CHANNELS 2U
#define DELAY_BUFFER_FRAMES 18000U // 375ms stereo echo buffer in .bss
#define TWO_PI 6.28318530718f

// ============================================================================
// Hybrid Audio State (Static .bss storage — Zero Heap Allocation in Game Loop)
// ============================================================================

typedef struct {
  ma_data_source_base base;
  uint64_t cursor_frame;
  volatile int active_level;
  volatile int mute_bgm;       // 1 when a custom file is playing for the level
  volatile uint32_t sfx_frame; // Countdown in audio frames for Space/W/Up SFX
  volatile uint32_t sfx_total;
  volatile int sfx_level;
  uint32_t rng_state;
  uint32_t delay_idx;
  float lp_noise_l;
  float lp_noise_r;
} ProceduralSynth;

static ma_engine s_engine;
static ma_sound s_level_sounds[LEVEL_COUNT];
static bool s_level_has_file[LEVEL_COUNT];
static ProceduralSynth s_synth;
static ma_sound s_synth_sound;
static bool s_synth_ready = false;

// Stereo ping-pong delay line stored in .bss
static float s_delay_l[DELAY_BUFFER_FRAMES];
static float s_delay_r[DELAY_BUFFER_FRAMES];

// Per-level candidate audio files in assets/
static const char *const LEVEL_BGM_CANDIDATES[LEVEL_COUNT][6] = {
    // Level 1: MOONLIT VALLEY
    {"assets/valley.mp3", "assets/valley.wav", "assets/valley.ogg",
     "assets/level1.mp3", "assets/level1.wav", "assets/level1.ogg"},
    // Level 2: DEEP IN THE WATER
    {"assets/water.mp3", "assets/water.wav", "assets/water.ogg",
     "assets/level2.mp3", "assets/level2.wav", "assets/level2.ogg"},
    // Level 3: VICE
    {"assets/vice.mp3", "assets/vice.wav", "assets/vice.ogg",
     "assets/level3.mp3", "assets/level3.wav", "assets/level3.ogg"},
};

// Global fallback candidate paths if per-level files are not present
static const char *const GLOBAL_BGM_CANDIDATES[] = {
    "assets/music.mp3", "assets/music.wav", "assets/music.ogg",
    "assets/bgm.mp3",   "assets/bgm.wav",
};

// ============================================================================
// Waveform & DSP Helpers
// ============================================================================

static inline float synth_rand_bipolar(ProceduralSynth *syn) {
  syn->rng_state = syn->rng_state * 1664525U + 1013904223U;
  return ((float)(syn->rng_state >> 8U) / 8388608.0f) - 1.0f;
}

static inline float osc_sine(float t) {
  return sinf(t * TWO_PI);
}

static inline float osc_triangle(float t) {
  float frac = t - floorf(t);
  return 1.0f - 4.0f * fabsf(frac - 0.5f);
}

static inline float osc_saw(float t) {
  float frac = t - floorf(t);
  return 2.0f * frac - 1.0f;
}

static inline float osc_pulse(float t, float duty) {
  float frac = t - floorf(t);
  return (frac < duty) ? 1.0f : -1.0f;
}

static inline float clamp_sample(float x) {
  return x < -0.95f ? -0.95f : (x > 0.95f ? 0.95f : x);
}

// ============================================================================
// Per-Level Procedural Music Themes
// ============================================================================

// Level 1: MOONLIT VALLEY — Nocturnal CRT ambient pad + E-minor arpeggio + breeze
static void synth_level_moonlit_valley(ProceduralSynth *syn, double t,
                                       float *out_l, float *out_r) {
  // 96 BPM -> 1.6 beats/sec -> 6.4 sixteenth-notes/sec
  double step_rate = 6.4;
  double step_pos = t * step_rate;
  int step = (int)step_pos;
  float frac = (float)(step_pos - (double)step);
  int bar = (step / 16) % 4;
  int arp_idx = step % 8;

  // 4-bar root bass progression: E2 -> C2 -> G2 -> D2
  static const float bass_freqs[4] = {82.41f, 65.41f, 98.00f, 73.42f};
  // Mystical nocturnal arpeggio per bar
  static const float arp_table[4][8] = {
      {329.63f, 392.00f, 493.88f, 659.25f, 587.33f, 493.88f, 392.00f, 493.88f},
      {261.63f, 329.63f, 392.00f, 523.25f, 493.88f, 392.00f, 329.63f, 392.00f},
      {293.66f, 392.00f, 493.88f, 587.33f, 659.25f, 587.33f, 493.88f, 392.00f},
      {293.66f, 369.99f, 440.00f, 587.33f, 523.25f, 440.00f, 369.99f, 440.00f},
  };

  float f_bass = bass_freqs[bar];
  float f_arp = arp_table[bar][arp_idx];

  // Warm triangle + sub-sine bass pad
  float bass = 0.55f * osc_triangle((float)(t * (double)f_bass)) +
               0.45f * osc_sine((float)(t * (double)(f_bass * 0.5f)));

  // Plucky CRT pulse/triangle lead with subtle vibrato
  float env = expf(-frac * 4.2f) * (1.0f - expf(-frac * 60.0f));
  float vib = 1.0f + 0.003f * sinf((float)t * 32.0f);
  float lead = (0.65f * osc_triangle((float)(t * (double)(f_arp * vib))) +
                0.35f * osc_pulse((float)(t * (double)f_arp), 0.28f)) *
               env;

  // High shimmering fifth pad
  float pad = 0.25f * osc_sine((float)(t * (double)(f_bass * 3.0f))) *
              (0.6f + 0.4f * sinf((float)t * 1.4f));

  // Soft night valley wind murmur (low-passed white noise)
  float n = synth_rand_bipolar(syn);
  syn->lp_noise_l += 0.018f * (n - syn->lp_noise_l);
  float breeze = syn->lp_noise_l * (0.04f + 0.025f * sinf((float)t * 0.7f));

  float pan = 0.5f + 0.35f * sinf((float)step * 0.9f);
  *out_l = bass * 0.16f + pad * 0.08f + lead * 0.14f * pan + breeze;
  *out_r = bass * 0.16f + pad * 0.08f + lead * 0.14f * (1.0f - pan) + breeze;
}

// Level 2: DEEP IN THE WATER — Bioluminescent cavern pad + 1.6s water drop/splash
static void synth_level_deep_in_the_water(ProceduralSynth *syn, double t,
                                          float *out_l, float *out_r) {
  // Synchronized with render.c: 96 ticks @ 60 FPS = 1.60 seconds per drop cycle
  double cycle_sec = 1.60;
  double cycle_phase = fmod(t, cycle_sec);
  int cycle_idx = (int)(t / cycle_sec) % 4;

  // Deep abyssal binaural drone (Dm9 -> Bbmaj7 -> Gm9 -> A7sus4)
  static const float chord_roots[4] = {73.42f, 58.27f, 98.00f, 55.00f};
  static const float chord_hi[4][3] = {
      {220.00f, 261.63f, 329.63f},
      {174.61f, 220.00f, 261.63f},
      {196.00f, 233.08f, 293.66f},
      {164.81f, 220.00f, 246.94f},
  };

  float root = chord_roots[cycle_idx];
  float sub = osc_sine((float)(t * (double)root)) * 0.15f;

  // Slowly breathing bioluminescent aquatic pad with stereo detune
  float swell = 0.55f + 0.45f * sinf((float)(cycle_phase / cycle_sec * TWO_PI));
  float pad_l = (osc_sine((float)(t * (double)chord_hi[cycle_idx][0])) +
                 0.7f * osc_sine((float)(t * (double)chord_hi[cycle_idx][1])) +
                 0.5f * osc_triangle(
                            (float)(t * (double)(chord_hi[cycle_idx][2] * 1.002f)))) *
                0.045f * swell;
  float pad_r = (osc_sine((float)(t * (double)(chord_hi[cycle_idx][0] * 1.003f))) +
                 0.7f * osc_sine((float)(t * (double)chord_hi[cycle_idx][1])) +
                 0.5f * osc_triangle((float)(t * (double)chord_hi[cycle_idx][2]))) *
                0.045f * swell;

  // Water droplet impact at tick 52/96 (~0.866s into the 1.6s cycle)
  float drop_sig = 0.0f;
  float splash_l = 0.0f;
  float splash_r = 0.0f;
  double impact_t = cycle_phase - 0.866;

  if (impact_t >= 0.0 && impact_t < 0.55) {
    float dt = (float)impact_t;
    // Classic upward pitch-chirp cavern droplet ("bloop")
    if (dt < 0.095f) {
      float p_norm = dt / 0.095f;
      float drop_freq = 360.0f + 680.0f * (p_norm * p_norm);
      float drop_env = expf(-dt * 34.0f) * sinf(p_norm * 3.14159265f);
      drop_sig = osc_sine(dt * drop_freq) * drop_env * 0.32f;
    }
    // Secondary Rayleigh rebound droplet 110ms later
    if (dt >= 0.11f && dt < 0.19f) {
      float dt2 = dt - 0.11f;
      float p2 = dt2 / 0.08f;
      float f2 = 520.0f + 540.0f * (p2 * p2);
      drop_sig += osc_sine(dt2 * f2) * expf(-dt2 * 40.0f) * 0.18f;
    }
    // Crown splash filtered water spray + expanding crystal ripple chime
    float n = synth_rand_bipolar(syn);
    syn->lp_noise_l += 0.14f * (n - syn->lp_noise_l);
    syn->lp_noise_r += 0.11f * (synth_rand_bipolar(syn) - syn->lp_noise_r);
    float spray_env = expf(-dt * 9.5f) * (1.0f - expf(-dt * 90.0f));
    splash_l = syn->lp_noise_l * spray_env * 0.16f;
    splash_r = syn->lp_noise_r * spray_env * 0.16f;

    // Harmonic ripple shimmer
    float chime_f = chord_hi[cycle_idx][2] * 2.0f;
    float chime = osc_sine((float)(t * (double)chime_f)) * expf(-dt * 5.5f) * 0.07f;
    splash_l += chime;
    splash_r += chime * 0.85f;
  }

  *out_l = sub + pad_l + drop_sig + splash_l;
  *out_r = sub + pad_r + drop_sig + splash_r;
}

// Level 3: MEDIEVAL VICE — 112 BPM Dungeon-Synthwave (80s Bass + Dorian Lute/Brass)
static void synth_level_vice(ProceduralSynth *syn, double t, float *out_l,
                             float *out_r) {
  // 112 BPM -> 7.4667 sixteenth-notes per second
  double step_rate = 112.0 / 60.0 * 4.0;
  double step_pos = t * step_rate;
  int step = (int)step_pos;
  float frac = (float)(step_pos - (double)step);
  int bar = (step / 16) % 4;
  int step16 = step % 16;

  // Medieval D-Dorian / Synthwave progression: Dm -> C -> Bb -> C/A
  static const float bass_roots[4] = {73.42f, 65.41f, 58.27f, 55.00f};
  static const float lead_notes[4][8] = {
      {293.66f, 349.23f, 440.00f, 587.33f, 523.25f, 493.88f, 440.00f, 349.23f},
      {261.63f, 329.63f, 392.00f, 523.25f, 493.88f, 440.00f, 392.00f, 329.63f},
      {233.08f, 293.66f, 349.23f, 466.16f, 440.00f, 392.00f, 349.23f, 293.66f},
      {220.00f, 293.66f, 329.63f, 440.00f, 392.00f, 349.23f, 329.63f, 261.63f},
  };

  // 1. Driving 16th-note 80s Miami octave-jumping saw/pulse synth bass
  float root = bass_roots[bar] * ((step16 % 2 == 1) ? 2.0f : 1.0f);
  float bass_env = expf(-frac * 5.5f);
  float bass = (0.6f * osc_saw((float)(t * (double)root)) +
                0.4f * osc_pulse((float)(t * (double)(root * 1.004f)), 0.35f)) *
               bass_env * 0.16f;

  // 2. War-drum kick + 80s gated snare + metallic chainmail hi-hats
  float drums = 0.0f;
  if (step16 == 0 || step16 == 8) {
    float kf = 46.0f + 125.0f * expf(-frac * 26.0f);
    drums += osc_sine(frac * kf * 0.25f) * expf(-frac * 8.0f) * 0.28f;
  }
  if (step16 == 4 || step16 == 12) {
    float n = synth_rand_bipolar(syn);
    float body = osc_triangle(frac * 170.0f * 0.14f) * expf(-frac * 13.0f);
    float gated_noise = n * expf(-frac * 6.5f);
    drums += (body * 0.45f + gated_noise * 0.55f) * 0.20f;
  }
  if (step16 % 2 == 1) {
    drums += synth_rand_bipolar(syn) * expf(-frac * 26.0f) * 0.05f;
  }

  // 3. Plucky harpsichord/synth-brass Dorian melody + fifth harmony
  float f_lead = lead_notes[bar][step % 8];
  float pluck_env = expf(-frac * 4.5f);
  float lead_l = (0.50f * osc_pulse((float)(t * (double)f_lead), 0.22f) +
                  0.35f * osc_saw((float)(t * (double)(f_lead * 1.003f))) +
                  0.15f * osc_triangle((float)(t * (double)(f_lead * 1.5f)))) *
                 pluck_env * 0.11f;
  float lead_r = (0.50f * osc_pulse((float)(t * (double)(f_lead * 0.997f)), 0.28f) +
                  0.35f * osc_saw((float)(t * (double)f_lead)) +
                  0.15f * osc_triangle((float)(t * (double)(f_lead * 1.5f)))) *
                 pluck_env * 0.11f;

  *out_l = bass + drums + lead_l;
  *out_r = bass + drums + lead_r;
}

// ============================================================================
// Interactive Action SFX (Space / W / Up)
// ============================================================================

static void synth_action_sfx(ProceduralSynth *syn, float *io_l, float *io_r) {
  uint32_t rem = syn->sfx_frame;
  uint32_t tot = syn->sfx_total;
  if (rem == 0 || tot == 0)
    return;

  syn->sfx_frame = rem - 1;
  float u = (float)(tot - rem) / (float)tot; // 0.0 -> 1.0 normalized progress
  float sfx = 0.0f;

  if (syn->sfx_level == LEVEL_MOONLIT_VALLEY) {
    // Dog parabolic jump: bright retro upward pitch bend + soft paw landing
    if (u < 0.45f) {
      float ju = u / 0.45f;
      float freq = 220.0f + 420.0f * (ju * (2.0f - ju));
      sfx = osc_triangle(u * freq * 0.45f) * (1.0f - ju) * 0.24f;
    } else if (u > 0.85f) {
      float lu = (u - 0.85f) / 0.15f;
      sfx = osc_sine(lu * 95.0f * 0.12f) * (1.0f - lu) * 0.18f;
    }
  } else if (syn->sfx_level == LEVEL_DEEP_IN_THE_WATER) {
    // Leaping Koi Fish: launch splash -> sparkling marimba arc -> re-entry splash
    if (u < 0.16f) {
      float su = u / 0.16f;
      float n = synth_rand_bipolar(syn);
      sfx = (n * 0.4f + osc_sine(su * 520.0f * 0.15f) * 0.6f) * (1.0f - su) *
            0.25f;
    } else if (u < 0.58f) {
      float au = (u - 0.16f) / 0.42f;
      static const float koi_notes[5] = {587.33f, 659.25f, 783.99f, 880.00f,
                                         1046.50f};
      int idx = (int)(au * 4.99f);
      float note_u = fmodf(au * 5.0f, 1.0f);
      sfx = osc_sine(au * koi_notes[idx] * 0.55f) * expf(-note_u * 4.0f) * 0.18f;
    } else if (u < 0.80f) {
      float ru = (u - 0.58f) / 0.22f;
      float drop_f = 320.0f + 560.0f * ru * ru;
      sfx = (osc_sine(ru * drop_f * 0.2f) * 0.65f +
             synth_rand_bipolar(syn) * 0.35f) *
            expf(-ru * 4.5f) * 0.24f;
    }
  } else {
    // Level 3 VICE: Dual-tone Miami police siren & neon lens flash shimmer
    float siren_f = 620.0f + 280.0f * sinf(u * TWO_PI * 2.5f);
    float env = sinf(u * 3.14159265f);
    sfx = (0.6f * osc_triangle(u * siren_f * 0.5f) +
           0.4f * osc_saw(u * siren_f * 0.502f)) *
          env * 0.22f;
  }

  *io_l += sfx;
  *io_r += sfx;
}

// ============================================================================
// miniaudio Custom Data Source Implementation
// ============================================================================

static ma_result synth_on_read(ma_data_source *pDataSource, void *pFramesOut,
                               ma_uint64 frameCount, ma_uint64 *pFramesRead) {
  ProceduralSynth *syn = (ProceduralSynth *)pDataSource;
  float *out = (float *)pFramesOut;

  for (ma_uint64 i = 0; i < frameCount; i++) {
    double t = (double)syn->cursor_frame / (double)SYNTH_SAMPLE_RATE;
    syn->cursor_frame++;

    float dry_l = 0.0f;
    float dry_r = 0.0f;

    if (!syn->mute_bgm) {
      int lvl = syn->active_level;
      if (lvl == LEVEL_DEEP_IN_THE_WATER) {
        synth_level_deep_in_the_water(syn, t, &dry_l, &dry_r);
      } else if (lvl == LEVEL_VICE) {
        synth_level_vice(syn, t, &dry_l, &dry_r);
      } else {
        synth_level_moonlit_valley(syn, t, &dry_l, &dry_r);
      }
    }

    synth_action_sfx(syn, &dry_l, &dry_r);

    // Stereo ping-pong echo / reverb
    uint32_t d_idx = syn->delay_idx;
    float echo_l = s_delay_l[d_idx];
    float echo_r = s_delay_r[d_idx];

    s_delay_l[d_idx] = dry_l + echo_r * 0.34f;
    s_delay_r[d_idx] = dry_r + echo_l * 0.34f;
    syn->delay_idx = (d_idx + 1U) % DELAY_BUFFER_FRAMES;

    out[i * 2 + 0] = clamp_sample(dry_l + echo_l * 0.28f);
    out[i * 2 + 1] = clamp_sample(dry_r + echo_r * 0.28f);
  }

  if (pFramesRead != NULL) {
    *pFramesRead = frameCount;
  }
  return MA_SUCCESS;
}

static ma_result synth_on_seek(ma_data_source *pDataSource,
                               ma_uint64 frameIndex) {
  ProceduralSynth *syn = (ProceduralSynth *)pDataSource;
  syn->cursor_frame = frameIndex;
  return MA_SUCCESS;
}

static ma_result synth_on_get_data_format(ma_data_source *pDataSource,
                                          ma_format *pFormat,
                                          ma_uint32 *pChannels,
                                          ma_uint32 *pSampleRate,
                                          ma_channel *pChannelMap,
                                          size_t channelMapCap) {
  (void)pDataSource;
  (void)pChannelMap;
  (void)channelMapCap;
  if (pFormat != NULL)
    *pFormat = ma_format_f32;
  if (pChannels != NULL)
    *pChannels = SYNTH_CHANNELS;
  if (pSampleRate != NULL)
    *pSampleRate = SYNTH_SAMPLE_RATE;
  return MA_SUCCESS;
}

static ma_result synth_on_get_cursor(ma_data_source *pDataSource,
                                     ma_uint64 *pCursor) {
  ProceduralSynth *syn = (ProceduralSynth *)pDataSource;
  if (pCursor != NULL)
    *pCursor = syn->cursor_frame;
  return MA_SUCCESS;
}

static ma_result synth_on_get_length(ma_data_source *pDataSource,
                                     ma_uint64 *pLength) {
  (void)pDataSource;
  if (pLength != NULL)
    *pLength = 0;
  return MA_NOT_IMPLEMENTED;
}

static const ma_data_source_vtable s_synth_vtable = {
    .onRead = synth_on_read,
    .onSeek = synth_on_seek,
    .onGetDataFormat = synth_on_get_data_format,
    .onGetCursor = synth_on_get_cursor,
    .onGetLength = synth_on_get_length,
    .onSetLooping = NULL,
    .flags = 0,
};

// ============================================================================
// Public Audio API
// ============================================================================

static bool try_load_sound_for_level(LevelId lvl, const char *explicit_path) {
  // 1. Try level-specific candidates in assets/
  for (size_t i = 0; i < 6; i++) {
    const char *path = LEVEL_BGM_CANDIDATES[lvl][i];
    if (ma_sound_init_from_file(&s_engine, path, MA_SOUND_FLAG_STREAM, NULL,
                                NULL, &s_level_sounds[lvl]) == MA_SUCCESS) {
      ma_sound_set_looping(&s_level_sounds[lvl], MA_TRUE);
      return true;
    }
  }

  // 2. Try explicit CLI path if provided
  if (explicit_path != NULL) {
    if (ma_sound_init_from_file(&s_engine, explicit_path, MA_SOUND_FLAG_STREAM,
                                NULL, NULL,
                                &s_level_sounds[lvl]) == MA_SUCCESS) {
      ma_sound_set_looping(&s_level_sounds[lvl], MA_TRUE);
      return true;
    }
  }

  // 3. Try global fallback candidates in assets/
  size_t global_count =
      sizeof(GLOBAL_BGM_CANDIDATES) / sizeof(GLOBAL_BGM_CANDIDATES[0]);
  for (size_t i = 0; i < global_count; i++) {
    if (ma_sound_init_from_file(&s_engine, GLOBAL_BGM_CANDIDATES[i],
                                MA_SOUND_FLAG_STREAM, NULL, NULL,
                                &s_level_sounds[lvl]) == MA_SUCCESS) {
      ma_sound_set_looping(&s_level_sounds[lvl], MA_TRUE);
      return true;
    }
  }

  return false;
}

void audio_init(GameState *state, const char *bgm_path) {
  state->audio_ready = false;
  state->music_loaded = false;
  state->is_muted = false;
  s_synth_ready = false;

  if (ma_engine_init(NULL, &s_engine) != MA_SUCCESS) {
    return;
  }
  state->audio_ready = true;

  // Probe custom audio files for each level
  for (int lvl = 0; lvl < LEVEL_COUNT; lvl++) {
    s_level_has_file[lvl] = try_load_sound_for_level((LevelId)lvl, bgm_path);
    if (s_level_has_file[lvl]) {
      state->music_loaded = true;
    }
  }

  // Initialize built-in procedural synthesizer data source
  ma_data_source_config cfg = ma_data_source_config_init();
  cfg.vtable = &s_synth_vtable;
  if (ma_data_source_init(&cfg, &s_synth.base) == MA_SUCCESS) {
    s_synth.cursor_frame = 0;
    s_synth.active_level = (int)state->current_level;
    s_synth.mute_bgm = s_level_has_file[state->current_level] ? 1 : 0;
    s_synth.sfx_frame = 0;
    s_synth.sfx_total = 0;
    s_synth.sfx_level = (int)state->current_level;
    s_synth.rng_state = 0xC0FFEE11U;
    s_synth.delay_idx = 0;
    s_synth.lp_noise_l = 0.0f;
    s_synth.lp_noise_r = 0.0f;

    if (ma_sound_init_from_data_source(&s_engine, &s_synth.base, 0, NULL,
                                       &s_synth_sound) == MA_SUCCESS) {
      s_synth_ready = true;
      state->music_loaded = true;
      ma_sound_start(&s_synth_sound);
    } else {
      ma_data_source_uninit(&s_synth.base);
    }
  }

  audio_set_level(state, state->current_level);
}

void audio_set_level(GameState *state, LevelId level) {
  if (!state->audio_ready || level >= LEVEL_COUNT) {
    return;
  }

  for (int i = 0; i < LEVEL_COUNT; i++) {
    if (s_level_has_file[i] && i != (int)level) {
      ma_sound_stop(&s_level_sounds[i]);
    }
  }

  s_synth.active_level = (int)level;
  s_synth.mute_bgm = s_level_has_file[level] ? 1 : 0;

  if (s_level_has_file[level] && !state->is_paused) {
    ma_sound_seek_to_pcm_frame(&s_level_sounds[level], 0);
    ma_sound_start(&s_level_sounds[level]);
  }
}

void audio_trigger_action(GameState *state) {
  if (!state->audio_ready || !s_synth_ready) {
    return;
  }
  LevelId lvl = state->current_level;
  uint32_t duration_frames =
      (lvl == LEVEL_DEEP_IN_THE_WATER)
          ? (SYNTH_SAMPLE_RATE * FISH_JUMP_DURATION_TICKS) / TARGET_FPS
          : (SYNTH_SAMPLE_RATE * JUMP_DURATION_TICKS) / TARGET_FPS;

  s_synth.sfx_level = (int)lvl;
  s_synth.sfx_total = duration_frames;
  s_synth.sfx_frame = duration_frames;
}

void audio_set_paused(GameState *state, bool paused) {
  if (!state->audio_ready || !state->music_loaded) {
    return;
  }
  LevelId lvl = state->current_level;
  if (paused) {
    if (lvl < LEVEL_COUNT && s_level_has_file[lvl]) {
      ma_sound_stop(&s_level_sounds[lvl]);
    }
    if (s_synth_ready) {
      ma_sound_stop(&s_synth_sound);
    }
  } else {
    if (lvl < LEVEL_COUNT && s_level_has_file[lvl]) {
      ma_sound_start(&s_level_sounds[lvl]);
    }
    if (s_synth_ready) {
      ma_sound_start(&s_synth_sound);
    }
  }
}

void audio_toggle_mute(GameState *state) {
  if (!state->audio_ready) {
    return;
  }
  state->is_muted = !state->is_muted;
  ma_engine_set_volume(&s_engine, state->is_muted ? 0.0f : 1.0f);
}

void audio_cleanup(GameState *state) {
  if (!state->audio_ready) {
    return;
  }
  if (s_synth_ready) {
    ma_sound_uninit(&s_synth_sound);
    ma_data_source_uninit(&s_synth.base);
    s_synth_ready = false;
  }
  for (int i = 0; i < LEVEL_COUNT; i++) {
    if (s_level_has_file[i]) {
      ma_sound_uninit(&s_level_sounds[i]);
      s_level_has_file[i] = false;
    }
  }
  state->music_loaded = false;
  ma_engine_uninit(&s_engine);
  state->audio_ready = false;
}
