#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#define TB_OPT_ATTR_W 32
#define TB_IMPL
#include "termbox.h"
#undef TB_IMPL
#pragma clang diagnostic pop

#include "game.h"
#include "sprites.h"
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Deep CRT obsidian-green void background matching reference image
static const ColorRGB BG_BASE = {4, 8, 5};

static inline ColorRGB rgb(uint8_t r, uint8_t g, uint8_t b) {
  ColorRGB c = {r, g, b};
  return c;
}

static inline uintattr_t rgb_to_tb(ColorRGB c) {
  uintattr_t hex =
      ((uintattr_t)c.r << 16) | ((uintattr_t)c.g << 8) | (uintattr_t)c.b;
  return (hex == 0) ? TB_HI_BLACK : hex;
}

static inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static inline int clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static inline ColorRGB lerp_rgb(ColorRGB a, ColorRGB b, float t) {
  t = clampf(t, 0.0f, 1.0f);
  ColorRGB out = {
      (uint8_t)((float)a.r + ((float)b.r - (float)a.r) * t + 0.5f),
      (uint8_t)((float)a.g + ((float)b.g - (float)a.g) * t + 0.5f),
      (uint8_t)((float)a.b + ((float)b.b - (float)a.b) * t + 0.5f),
  };
  return out;
}

// Encodes an 8-bit Braille sub-pixel mask into UTF-8 (U+2800 .. U+28FF)
static inline void encode_braille_utf8(uint8_t mask, char out[5]) {
  out[0] = (char)0xE2;
  out[1] = (char)(0xA0 | ((mask >> 6) & 0x03));
  out[2] = (char)(0x80 | (mask & 0x3F));
  out[3] = '\0';
  out[4] = '\0';
}

static inline void set_cell_ascii(int y, int x, char ch, ColorRGB fg,
                                  ColorRGB bg) {
  tb_set_cell(x, y, (uint32_t)(unsigned char)ch, rgb_to_tb(fg), rgb_to_tb(bg));
}

// Updates glyph and foreground while preserving existing cell background
static inline void set_cell_fg(int y, int x, char ch, ColorRGB fg) {
  struct tb_cell *cell = NULL;
  if (tb_get_cell(x, y, 1, &cell) == TB_OK && cell != NULL) {
    tb_set_cell(x, y, (uint32_t)(unsigned char)ch, rgb_to_tb(fg), cell->bg);
  }
}

static inline void set_cell_bg(int y, int x, ColorRGB bg) {
  struct tb_cell *cell = NULL;
  if (tb_get_cell(x, y, 1, &cell) == TB_OK && cell != NULL) {
    tb_set_cell(x, y, cell->ch, cell->fg, rgb_to_tb(bg));
  }
}

static inline void set_cell_utf8(int y, int x, const char *utf8, ColorRGB fg,
                                 ColorRGB bg) {
  uint32_t cp = ' ';
  if (tb_utf8_char_to_unicode(&cp, utf8) > 0) {
    tb_set_cell(x, y, cp, rgb_to_tb(fg), rgb_to_tb(bg));
  }
}

static inline void set_cell_utf8_fg(int y, int x, const char *utf8,
                                    ColorRGB fg) {
  struct tb_cell *cell = NULL;
  uint32_t cp = ' ';
  if (tb_get_cell(x, y, 1, &cell) == TB_OK && cell != NULL &&
      tb_utf8_char_to_unicode(&cp, utf8) > 0) {
    tb_set_cell(x, y, cp, rgb_to_tb(fg), cell->bg);
  }
}

// Fast 2D integer hash -> [0.0, 1.0] for procedural noise & Braille dithering
static inline float hash2d(int x, int y) {
  uint32_t h = (uint32_t)(x * 374761393U + y * 668265263U);
  h = (h ^ (h >> 13U)) * 1274126177U;
  h = h ^ (h >> 16U);
  return (float)(h & 0xFFFFU) / 65535.0f;
}

// Smooth 2D Value Noise for procedural planet surface & cliff strata
static float value_noise2d(float x, float y) {
  int xi = (int)floorf(x);
  int yi = (int)floorf(y);
  float xf = x - (float)xi;
  float yf = y - (float)yi;
  float u = xf * xf * (3.0f - 2.0f * xf);
  float v = yf * yf * (3.0f - 2.0f * yf);

  float n00 = hash2d(xi, yi);
  float n10 = hash2d(xi + 1, yi);
  float n01 = hash2d(xi, yi + 1);
  float n11 = hash2d(xi + 1, yi + 1);

  float nx0 = n00 + u * (n10 - n00);
  float nx1 = n01 + u * (n11 - n01);
  return nx0 + v * (nx1 - nx0);
}

int render_init(void) {
  setlocale(LC_ALL, "");
  int rv = tb_init();
  if (rv != TB_OK) {
    return rv;
  }
  tb_set_output_mode(TB_OUTPUT_TRUECOLOR);
  tb_set_clear_attrs(rgb_to_tb(BG_BASE), rgb_to_tb(BG_BASE));
  tb_hide_cursor();
  tb_clear();
  tb_present();
  return TB_OK;
}

static void draw_sky_stars(int term_width, int ground_y, unsigned long tick) {
  int sky_height = ground_y - 4;
  if (sky_height < 4)
    return;

  int inner_w = term_width - 4;
  if (inner_w <= 0)
    return;

  // Parallax Stars with 3 depth layers + sinusoidal twinkling
  for (int i = 0; i < 32; i++) {
    int layer = i % 3; // 0 = far/slow, 1 = mid, 2 = near/bright
    int speed_div = (layer == 0) ? 24 : ((layer == 1) ? 16 : 10);
    int star_y = 2 + ((i * 11 + layer * 5) % sky_height);
    int star_x = 2 + (((i * 37 + 13) - (int)(tick / (unsigned long)speed_div)) %
                      inner_w);
    if (star_x < 2)
      star_x += inner_w;

    ColorRGB base_col =
        lerp_rgb(rgb(35, 90, 50), rgb(110, 225, 130), (float)layer / 2.0f);

    float phase = (float)tick * 0.08f + (float)i * 1.91f;
    float twinkle = 0.75f + 0.25f * sinf(phase);
    ColorRGB star_col = {
        (uint8_t)clampf((float)base_col.r * twinkle, 20.0f, 255.0f),
        (uint8_t)clampf((float)base_col.g * twinkle, 55.0f, 255.0f),
        (uint8_t)clampf((float)base_col.b * twinkle, 30.0f, 255.0f),
    };

    char star_ch = (layer == 2) ? '+' : ((layer == 1) ? '.' : '.');
    set_cell_fg(star_y, star_x, star_ch, star_col);
  }
}

// Draws a multi-row striated horizontal cloud bank (matching reference style)
static void draw_cloud_bank(int term_width, int ground_y, int center_x,
                            int base_y, int half_w, int height,
                            ColorRGB col_bright, ColorRGB col_dim,
                            bool opaque_bg) {
  for (int r = 0; r < height; r++) {
    int y = base_y - r;
    if (y < 2 || y >= ground_y - 1)
      continue;

    // Triangular / terraced profile: wider near bottom, tapering at top & very bottom
    float norm_r = (float)r / (float)(height > 1 ? height - 1 : 1);
    float taper = (r == 0) ? 0.72f : (1.0f - 0.82f * powf(norm_r, 1.35f));
    int row_half_w = (int)((float)half_w * taper);
    if (row_half_w < 2)
      continue;

    // Asymmetrical wind-sheared offset per row
    int row_shift = (int)(sinf((float)r * 1.7f) * 3.0f);
    int start_x = center_x - row_half_w + row_shift;
    int end_x = center_x + row_half_w + row_shift;

    char row_ch = (r % 3 == 0) ? '~' : ((r % 3 == 1) ? '=' : '-');
    ColorRGB row_col = lerp_rgb(col_dim, col_bright, 1.0f - norm_r * 0.55f);
    ColorRGB cloud_bg = rgb(6, 15, 9);

    for (int x = start_x; x <= end_x; x++) {
      if (x <= 1 || x >= term_width - 2)
        continue;

      int dist_edge = (x - start_x < end_x - x) ? (x - start_x) : (end_x - x);
      // Wispy broken edges on the left and right tips
      if (dist_edge < 4 && ((x + r * 3) % 3 == 0))
        continue;

      char ch = (dist_edge < 3) ? '.' : ((dist_edge < 6) ? '-' : row_ch);
      ColorRGB fg = (dist_edge < 4) ? col_dim : row_col;

      if (opaque_bg) {
        set_cell_ascii(y, x, ch, fg, cloud_bg);
      } else {
        set_cell_fg(y, x, ch, fg);
      }
    }
  }
}

// Shared Moon geometry helper so the Moon shader and overlapping foreground
// clouds scale identically from compact MacBook screens up to 4K monitors.
static void moon_geometry(int term_width, int ground_y, float *out_R,
                          float *out_cx, float *out_cy) {
  const float aspect_x = 0.48f;
  int sky_span = (ground_y - 2) - 3; // rows between HUD (y=2) and valley props
  if (sky_span < 8)
    sky_span = 8;

  float max_r_vert = (float)sky_span * 0.42f;
  float max_r_horiz = (float)term_width * 0.145f;
  float R = (max_r_vert < max_r_horiz) ? max_r_vert : max_r_horiz;
  R = clampf(R, 4.2f, 22.0f);

  float cx = (float)term_width - (R / aspect_x) * 1.18f - 4.0f;
  // Keep top corona clear of HUD row 2 (cy - 1.25*R >= 3.2)
  float cy = 3.2f + R * 1.18f;
  if (cy + R * 1.15f > (float)(ground_y - 2)) {
    cy = (float)(ground_y - 2) - R * 1.15f;
  }
  if (cy - R * 1.15f < 3.2f) {
    cy = 3.2f + R * 1.15f;
  }

  *out_R = R;
  *out_cx = cx;
  *out_cy = cy;
}

static void draw_clouds_background(int term_width, int ground_y,
                                   unsigned long tick) {
  int sky_h = ground_y - 4;
  if (sky_h < 8)
    return;

  int inner_w = term_width - 4;
  int drift_slow = (int)(tick / 14UL);
  int drift_mid = (int)(tick / 9UL);

  // 1. Large Upper-Left Striated Cloud Bank (high in the sky above the watcher)
  int c1_x = 2 + (((inner_w / 5) - drift_slow) % inner_w);
  if (c1_x < 2)
    c1_x += inner_w;
  int c1_y = 4 + sky_h / 6;
  draw_cloud_bank(term_width, ground_y, c1_x, c1_y,
                  clampi(inner_w / 6, 10, 42), clampi(sky_h / 6, 3, 8),
                  rgb(90, 195, 115), rgb(40, 100, 58), false);

  // 2. Lower Distant Valley Cloud Bank (between cliff and moon)
  int c2_x = 2 + (((inner_w / 2) - drift_slow) % inner_w);
  if (c2_x < 2)
    c2_x += inner_w;
  int c2_y = ground_y - sky_h / 4;
  draw_cloud_bank(term_width, ground_y, c2_x, c2_y,
                  clampi(inner_w / 8, 8, 30), clampi(sky_h / 7, 3, 6),
                  rgb(65, 145, 82), rgb(30, 78, 45), false);

  // 3. Mid-Sky Cloud Bank approaching the Moon's left limb
  int c3_x = 2 + ((((inner_w * 3) / 5) - drift_mid) % inner_w);
  if (c3_x < 2)
    c3_x += inner_w;
  int c3_y = 4 + (sky_h * 2) / 5;
  draw_cloud_bank(term_width, ground_y, c3_x, c3_y,
                  clampi(inner_w / 7, 9, 34), clampi(sky_h / 6, 3, 7),
                  rgb(85, 185, 105), rgb(38, 95, 55), false);
}

// Foreground clouds overlapping the Moon (like the cloud banks slicing across
// the moon's left and lower-right edges in the reference image)
static void draw_clouds_foreground(int term_width, int ground_y,
                                   unsigned long tick) {
  int sky_h = ground_y - 4;
  if (sky_h < 8)
    return;

  const float aspect_x = 0.48f;
  float R, cx, cy;
  moon_geometry(term_width, ground_y, &R, &cx, &cy);

  int drift = (int)((tick / 12UL) % 9UL) - 4;

  // Left-limb overlapping cloud bank
  int left_cloud_x = (int)(cx - (R / aspect_x) * 1.02f) + drift;
  int left_cloud_y = (int)(cy + R * 0.10f);
  draw_cloud_bank(term_width, ground_y, left_cloud_x, left_cloud_y,
                  clampi((int)(R * 1.55f), 8, 36),
                  clampi((int)(R * 0.45f), 3, 8), rgb(95, 205, 120),
                  rgb(42, 105, 60), true);

  // Lower-right limb overlapping cloud bank (extends to right viewport border)
  int right_cloud_x = (int)(cx + (R / aspect_x) * 0.72f) - drift;
  int right_cloud_y = (int)(cy + R * 0.52f);
  draw_cloud_bank(term_width, ground_y, right_cloud_x, right_cloud_y,
                  clampi((int)(R * 1.65f), 9, 38),
                  clampi((int)(R * 0.42f), 3, 8), rgb(100, 215, 125),
                  rgb(45, 110, 62), true);
}

// Procedural 3D Cratered Moon + Dark Mare Basins + Tycho Crater + Braille Glow
static void draw_planet_shader(int term_width, int ground_y,
                               unsigned long tick) {
  int avail_sky = ground_y - 4;
  if (avail_sky < 8 || term_width < 50)
    return;

  const float aspect_x = 0.48f;
  float R, cx, cy;
  moon_geometry(term_width, ground_y, &R, &cx, &cy);

  int min_y = (int)floorf(cy - R * 1.30f);
  int max_y = (int)ceilf(cy + R * 1.30f);
  int min_x = (int)floorf(cx - (R / aspect_x) * 1.30f);
  int max_x = (int)ceilf(cx + (R / aspect_x) * 1.30f);

  // Never overwrite HUD rows (1..2) or valley floor
  if (min_y < 3)
    min_y = 3;
  if (max_y >= ground_y - 1)
    max_y = ground_y - 2;
  if (min_x < 2)
    min_x = 2;
  if (max_x > term_width - 3)
    max_x = term_width - 3;

  static const uint8_t braille_bits[4][2] = {
      {0x01, 0x08},
      {0x02, 0x10},
      {0x04, 0x20},
      {0x40, 0x80},
  };

  float rot = (float)tick * 0.0025f;

  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
      float dx = ((float)x - cx) * aspect_x;
      float dy = (float)y - cy;
      float dist_norm = sqrtf(dx * dx + dy * dy) / R;

      if (dist_norm > 1.26f)
        continue;

      if (dist_norm > 1.0f) {
        // --- OUTER ATMOSPHERIC CORONA (1.0 < dist_norm <= 1.26) ---
        float t_corona = (dist_norm - 1.0f) / 0.26f;
        float smooth_t = t_corona * t_corona * (3.0f - 2.0f * t_corona);

        ColorRGB bg_glow = lerp_rgb(rgb(24, 68, 36), BG_BASE, smooth_t);

        uint8_t mask = 0;
        int drift = (int)(tick / 24UL);
        for (int sy = 0; sy < 4; sy++) {
          for (int sx = 0; sx < 2; sx++) {
            float sub_x = (float)x + ((float)sx - 0.25f) * 0.5f;
            float sub_y = (float)y + ((float)sy - 1.5f) * 0.25f;
            float sdx = (sub_x - cx) * aspect_x;
            float sdy = sub_y - cy;
            float sdist = sqrtf(sdx * sdx + sdy * sdy) / R;

            if (sdist > 1.0f && sdist <= 1.24f) {
              float st = (sdist - 1.0f) / 0.24f;
              float density = (1.0f - st) * (1.0f - st) * 0.80f;
              float rnd = hash2d(x * 2 + sx + drift, y * 4 + sy);
              if (rnd < density) {
                mask |= braille_bits[sy][sx];
              }
            }
          }
        }

        if (mask != 0) {
          char braille_glyph[5];
          encode_braille_utf8(mask, braille_glyph);
          ColorRGB fg_corona =
               lerp_rgb(rgb(115, 225, 135), rgb(22, 65, 34), t_corona);
          set_cell_utf8(y, x, braille_glyph, fg_corona, bg_glow);
        } else {
          set_cell_bg(y, x, bg_glow);
        }
      } else {
        // --- INNER 3D LUNAR SURFACE (dist_norm <= 1.0) ---
        float nx = dx / R;
        float ny = dy / R;
        float nz = sqrtf(clampf(1.0f - dist_norm * dist_norm, 0.0f, 1.0f));

        // 1. Bright outer lunar limb
        float rim = powf(1.0f - nz, 1.85f);

        // 2. Spherical coordinates & 3-octave organic lunar topography
        float u_sphere = atan2f(nx, nz) * 2.2f - rot;
        float v_sphere = asinf(clampf(ny, -1.0f, 1.0f)) * 2.2f;

        float n_coarse =
            value_noise2d(u_sphere * 1.8f + 10.0f, v_sphere * 1.8f);
        float n_mid = value_noise2d(u_sphere * 4.2f + 23.0f, v_sphere * 4.2f);
        float n_fine = value_noise2d(u_sphere * 9.5f + 41.0f, v_sphere * 9.5f);
        float terrain = n_coarse * 0.50f + n_mid * 0.32f + n_fine * 0.18f;

        // 3. Irregular Dark Lunar Maria Basins (warped by noise so they never look like circles)
        float warp_x = nx + (n_coarse - 0.5f) * 0.22f;
        float warp_y = ny + (n_mid - 0.5f) * 0.22f;
        float m1 = expf(-((warp_x + 0.28f) * (warp_x + 0.28f) * 5.2f +
                          (warp_y + 0.22f) * (warp_y + 0.22f) * 5.8f));
        float m2 = expf(-((warp_x + 0.36f) * (warp_x + 0.36f) * 6.2f +
                          (warp_y - 0.28f) * (warp_y - 0.28f) * 6.0f));
        float m3 = expf(-((warp_x - 0.18f) * (warp_x - 0.18f) * 4.8f +
                          (warp_y + 0.08f) * (warp_y + 0.08f) * 5.5f));
        float m4 = expf(-((warp_x - 0.42f) * (warp_x - 0.42f) * 7.5f +
                          (warp_y - 0.08f) * (warp_y - 0.08f) * 6.5f));
        float maria =
            clampf(m1 + m2 * 0.85f + m3 * 0.80f + m4 * 0.72f, 0.0f, 1.0f);

        // 4. Prominent Southern Tycho Crater + Ejecta Rays (lower-left highlands)
        float cdx = nx + 0.15f;
        float cdy = ny - 0.58f;
        float cdist = sqrtf(cdx * cdx + cdy * cdy);
        float tycho_ring = (cdist > 0.06f && cdist < 0.13f)
                               ? 0.42f
                               : ((cdist <= 0.06f) ? -0.22f : 0.0f);
        float south_highlands =
            expf(-((nx + 0.05f) * (nx + 0.05f) * 2.2f +
                   (ny - 0.60f) * (ny - 0.60f) * 2.8f)) *
            0.40f;

        float base_light =
            clampf(0.46f + nx * 0.24f + ny * 0.18f + nz * 0.28f, 0.1f, 1.0f);
        float lum = base_light * (0.52f + 0.48f * terrain) + south_highlands +
                    tycho_ring;
        lum = lum * (1.0f - 0.78f * maria);
        lum = clampf(lum + rim * 0.82f, 0.03f, 1.0f);

        ColorRGB fg_col;
        if (lum < 0.35f) {
          fg_col = lerp_rgb(rgb(18, 65, 30), rgb(70, 165, 90), lum / 0.35f);
        } else {
          fg_col = lerp_rgb(rgb(70, 165, 90), rgb(175, 255, 190),
                            (lum - 0.35f) / 0.65f);
        }
        ColorRGB bg_col = lerp_rgb(rgb(5, 12, 7), rgb(36, 92, 50),
                                   clampf(lum * lum, 0.0f, 1.0f));

        // Blend Braille sub-pixel crater dithering with high-density ASCII highlands
        // (avoids concentric '000'/'(((' banding on large monitors)
        float cell_rnd = hash2d(x * 5 + 11, y * 7 + 19);
        if (lum > 0.72f) {
          static const char hi_chars[] = "@#%*0@";
          char out_ch = hi_chars[(int)(cell_rnd * 5.99f)];
          set_cell_ascii(y, x, out_ch, fg_col, bg_col);
        } else if (lum > 0.18f) {
          uint8_t mask = 0;
          for (int sy = 0; sy < 4; sy++) {
            for (int sx = 0; sx < 2; sx++) {
              float hr = hash2d(x * 4 + sx * 13, y * 8 + sy * 17);
              if (hr < lum * 1.05f) {
                mask |= braille_bits[sy][sx];
              }
            }
          }
          char braille_glyph[5];
          encode_braille_utf8(mask, braille_glyph);
          set_cell_utf8(y, x, braille_glyph, fg_col, bg_col);
        } else {
          char out_ch = (cell_rnd > 0.80f) ? '.' : ' ';
          set_cell_ascii(y, x, out_ch, fg_col, bg_col);
        }
      }
    }
  }
}

// Helper to draw a multi-row ASCII prop at (top_y, left_x)
static void draw_prop_sprite(int term_width, int top_y, int left_x,
                             int prop_type, ColorRGB border_col,
                             ColorRGB fill_col) {
  const char *const *sprite = PROP_SPRITES[prop_type % PROP_TYPE_COUNT];
  for (int row = 0; row < PROP_HEIGHT; row++) {
    const char *line = sprite[row];
    for (int col = 0; line[col] != '\0'; col++) {
      int draw_x = left_x + col;
      char ch = line[col];
      if (ch != ' ' && draw_x > 1 && draw_x < term_width - 2) {
        bool is_highlight = (ch == '8' || ch == 'X' || ch == '[' || ch == ']' ||
                             ch == '\\' || ch == '/' || ch == '|');
        ColorRGB fg = is_highlight ? border_col : fill_col;
        set_cell_fg(top_y + row, draw_x, ch, fg);
      }
    }
  }
}

// Draws the scrolling valley plain and ruins/scrub to the right of the cliff
static void draw_valley_and_props(int term_width, int ground_y,
                                  int cliff_base_x, unsigned long tick) {
  int start_x = cliff_base_x - 2;
  if (start_x < 2)
    start_x = 2;

  int valley_w = term_width - 2 - start_x;
  if (valley_w <= 4)
    return;

  int scroll = (int)(tick / 2UL);

  // 1. Scrolling Surface Grass Blades & Micro-Tufts at (ground_y - 1)
  ColorRGB tuft_bright = rgb(110, 235, 130);
  ColorRGB tuft_dim = rgb(55, 145, 75);
  for (int x = start_x; x < term_width - 2; x++) {
    int wx = x + scroll;
    if (wx % 5 == 0) {
      char tch = (wx % 15 == 0) ? '/' : ((wx % 10 == 0) ? '\\' : '|');
      set_cell_fg(ground_y - 1, x, tch, tuft_dim);
    } else if (wx % 13 == 0) {
      set_cell_fg(ground_y - 1, x, '"', tuft_bright);
    } else if (wx % 9 == 0) {
      set_cell_fg(ground_y - 1, x, ',', tuft_dim);
    }
  }

  // 2. Scrolling Valley Props (Ruin Monolith 88888888, Scrub Bushes, Outcrops)
  ColorRGB prop_bright = rgb(135, 250, 155);
  ColorRGB prop_mid = rgb(70, 170, 92);
  int span = valley_w + 36;
  for (int p = 0; p < 4; p++) {
    int raw_x = ((p * (span / 4)) - scroll) % span;
    if (raw_x < 0)
      raw_x += span;
    int prop_x = start_x + raw_x - 16;
    if (prop_x < start_x - 4)
      continue;

    int top_y = ground_y - PROP_HEIGHT;
    draw_prop_sprite(term_width, top_y, prop_x, p % PROP_TYPE_COUNT,
                     prop_bright, prop_mid);
  }

  // 3. Stratified Horizontal Valley Ground Layers (matching reference bands)
  // Row 0 (ground_y):     Bright continuous grassy rim (" / = / #)
  // Row 1 (ground_y + 1): Dense striated topsoil (=== / ~~~)
  // Row 2 (ground_y + 2): Dark subsurface seam with sparse pebbles (. / -)
  // Row 3 (ground_y + 3): Vertical striated bedrock upper (| / : / !)
  // Row 4 (ground_y + 4): Vertical striated bedrock lower (| / : / .)
  const ColorRGB r0_fg = rgb(125, 245, 145);
  const ColorRGB r0_bg = rgb(16, 40, 22);
  const ColorRGB r1_fg = rgb(80, 180, 100);
  const ColorRGB r1_bg = rgb(12, 28, 16);
  const ColorRGB r2_fg = rgb(40, 95, 52);
  const ColorRGB r2_bg = BG_BASE;
  const ColorRGB r3_fg = rgb(65, 150, 82);
  const ColorRGB r3_bg = rgb(8, 18, 11);
  const ColorRGB r4_fg = rgb(45, 110, 60);
  const ColorRGB r4_bg = BG_BASE;

  for (int x = start_x; x < term_width - 2; x++) {
    int wx = x + scroll;

    // Row 0: Bright upper turf crust
    char c0 = (wx % 3 == 0) ? '"' : ((wx % 2 == 0) ? '=' : '#');
    set_cell_ascii(ground_y, x, c0, r0_fg, r0_bg);

    // Row 1: Striated soil sub-band
    char c1 = (wx % 4 == 0) ? '~' : '=';
    set_cell_ascii(ground_y + 1, x, c1, r1_fg, r1_bg);

    // Row 2: Dark seam
    char c2 = (wx % 11 == 0) ? '-' : ((wx % 7 == 0) ? '.' : ' ');
    set_cell_ascii(ground_y + 2, x, c2, r2_fg, r2_bg);

    // Row 3: Vertical striated bedrock columns
    char c3 = (wx % 3 == 0) ? '|' : ((wx % 2 == 0) ? ':' : '!');
    set_cell_ascii(ground_y + 3, x, c3, r3_fg, r3_bg);

    // Row 4: Deep vertical striations
    char c4 = (wx % 3 == 0) ? ':' : ((wx % 2 == 0) ? '|' : '.');
    set_cell_ascii(ground_y + 4, x, c4, r4_fg, r4_bg);
  }
}

// Draws the Left Craggy Cliff Promontory and the Boy standing on its edge
static void draw_cliff_and_watcher(const GameState *state, int cliff_top_y,
                                   int cliff_edge_x) {
  int bot_y = state->term_height - 2;
  int cliff_h = bot_y - cliff_top_y + 1;
  if (cliff_h < 5)
    return;

  // 1. Render the craggy cliff mass from cliff_top_y down to bot_y
  for (int y = cliff_top_y; y <= bot_y; y++) {
    int dy = y - cliff_top_y;
    float t_y = (float)dy / (float)(cliff_h > 1 ? cliff_h - 1 : 1);

    // Organic craggy cliff profile (matching reference promontory):
    // - Stepped upper plateau brow near top (dy == 0..2)
    // - Overhanging crag nose (t_y in 0.06 .. 0.28)
    // - Recessed concave rock face in mid-section (t_y in 0.30 .. 0.65)
    // - Wide sloping talus base spreading into the valley floor at the bottom
    int edge_offset = 0;
    int crag_jag = (int)(sinf((float)y * 1.35f) * 1.8f);
    if (dy == 0) {
      edge_offset = -2;
    } else if (t_y < 0.25f) {
      edge_offset = 1 + crag_jag;
    } else if (t_y < 0.66f) {
      edge_offset = -2 + crag_jag;
    } else {
      float t_base = (t_y - 0.66f) / 0.34f;
      int max_flare = clampi(cliff_edge_x / 4, 5, 14);
      edge_offset = -1 + (int)(t_base * t_base * (float)max_flare) + crag_jag / 2;
    }

    int row_right_x = cliff_edge_x + edge_offset;
    if (row_right_x >= state->term_width - 4)
      row_right_x = state->term_width - 4;

    // Stepped upper-left terrace: left side rises slightly higher than right lip
    for (int x = 2; x <= row_right_x; x++) {
      int dist_from_edge = row_right_x - x;
      float n_rock = value_noise2d((float)x * 0.32f, (float)y * 0.52f);
      float h_cell = hash2d(x * 7 + 3, y * 13 + 5);

      // Diagonal bright mineral veins (sloping down-right like in the reference)
      float vein_phase = (float)x * 0.42f - (float)y * 0.62f;
      bool on_vein = (sinf(vein_phase) > 0.76f) && (n_rock > 0.40f);

      ColorRGB fg;
      ColorRGB bg = BG_BASE;
      char ch = ' ';

      if (dy == 0) {
        // Organic fractured cliff plateau turf (no uniform ruler pattern)
        if (dist_from_edge > 12 && (h_cell < 0.35f))
          continue;
        static const char top_chars[] = ".,_\"=-#";
        ch = top_chars[(int)(h_cell * 6.99f)];
        fg = (dist_from_edge < 6) ? rgb(145, 255, 165) : rgb(75, 180, 100);
        bg = rgb(12, 32, 18);
      } else if (dy == 1 && x < row_right_x - 2) {
        static const char lip_chars[] = "=-#8:.[";
        ch = lip_chars[(int)(h_cell * 6.99f)];
        fg = lerp_rgb(rgb(60, 150, 82), rgb(115, 230, 135), n_rock);
        bg = rgb(10, 26, 15);
      } else if (dist_from_edge == 0) {
        // Moonlit extreme right cliff face rim
        ch = (y % 3 == 0) ? ']' : ((y % 2 == 0) ? '|' : ')');
        fg = rgb(160, 255, 180);
        bg = rgb(20, 52, 28);
      } else if (dist_from_edge <= 3) {
        // Illuminated right cliff face overhang & crags
        const char *rim_chars = "#8@|[]/\\=";
        ch = rim_chars[(x + y * 3) % 9];
        fg = lerp_rgb(rgb(145, 250, 165), rgb(80, 185, 100),
                      (float)dist_from_edge / 3.5f);
        bg = rgb(14, 38, 21);
      } else if (on_vein) {
        // Glowing diagonal rock strata blocks inside the cliff
        ch = (h_cell > 0.5f) ? '#' : ((h_cell > 0.25f) ? '=' : '|');
        fg = rgb(115, 235, 135);
        bg = rgb(18, 48, 26);
      } else if (n_rock > 0.56f) {
        // Vertical columnar rock joints & ledges
        if (y % 4 == 0) {
          ch = (x % 3 == 0) ? '=' : '-';
        } else {
          ch = (x % 4 == 0) ? '|' : ((x % 2 == 0) ? ':' : '!');
        }
        fg = lerp_rgb(rgb(35, 90, 48), rgb(85, 185, 105),
                      (n_rock - 0.56f) / 0.44f);
        bg = rgb(8, 20, 12);
      } else if (n_rock > 0.34f) {
        // Mid-tone fractured rock matrix
        const char *mid_chars = ".:;|+-=[]";
        ch = mid_chars[((unsigned int)(x * 11 + y * 7)) % 9U];
        fg = rgb(38, 95, 52);
      } else {
        // Deep dark cliff shadow cavities with sparse texture dots
        ch = (h_cell > 0.78f) ? '.' : ((h_cell > 0.68f) ? ':' : ' ');
        fg = rgb(24, 62, 34);
      }

      set_cell_ascii(y, x, ch, fg, bg);
    }
  }

  // 2. Scrub Bush nestled right at the foot of the cliff (matching reference)
  draw_prop_sprite(state->term_width, state->ground_y - PROP_HEIGHT,
                   cliff_edge_x + 1, 1, rgb(130, 245, 150), rgb(65, 160, 88));

  // 3. Draw the Watcher (Boy) standing on the cliff top edge looking right.
  // Clear interior cells between the left/right outline of each row so clouds
  // or stars never bleed through his head or coat.
  int boy_top_y = cliff_top_y - CLIFF_BOY_HEIGHT;
  int boy_left_x = cliff_edge_x - CLIFF_BOY_WIDTH - 2;
  if (boy_top_y >= 3 && boy_left_x >= 2) {
    int anim_frame = (int)((state->tick / 24UL) % CLIFF_BOY_FRAMES_COUNT);
    const char *const *boy_sprite = CLIFF_BOY_FRAMES[anim_frame];

    ColorRGB boy_highlight = rgb(185, 255, 200);
    ColorRGB boy_primary = rgb(125, 240, 145);

    for (int r = 0; r < CLIFF_BOY_HEIGHT; r++) {
      const char *line = boy_sprite[r];
      int first_c = -1, last_c = -1;
      for (int c = 0; line[c] != '\0'; c++) {
        if (line[c] != ' ') {
          if (first_c < 0)
            first_c = c;
          last_c = c;
        }
      }
      if (first_c < 0)
        continue;

      for (int c = first_c; c <= last_c; c++) {
        char ch = line[c];
        int dx = boy_left_x + c;
        int dy = boy_top_y + r;
        if (dx > 1 && dx < state->term_width - 2) {
          if (ch == ' ') {
            set_cell_ascii(dy, dx, ' ', BG_BASE, BG_BASE);
          } else {
            ColorRGB fg = (r <= 2 || ch == '>' || ch == '8') ? boy_highlight
                                                             : boy_primary;
            set_cell_ascii(dy, dx, ch, fg, BG_BASE);
          }
        }
      }
    }
  }
}

// Draws the Running & Jumping Dog on the Valley Floor with a solid silhouette
// that cleanly occludes scrolling background props behind it.
static void draw_dog(const GameState *state, int cliff_edge_x) {
  int valley_start = cliff_edge_x + 4;
  int avail_valley = state->term_width - 2 - valley_start;
  if (avail_valley < DOG_WIDTH + 4)
    return;

  // Position dog in the central valley plain (matching reference composition)
  int dog_x = valley_start + avail_valley / 3;

  // Compute parabolic vertical jump arc when jump_ticks > 0
  int jump_offset_y = 0;
  const char *const *sprite = NULL;

  if (state->jump_ticks > 0) {
    int elapsed = JUMP_DURATION_TICKS - state->jump_ticks;
    float t = (float)elapsed / (float)JUMP_DURATION_TICKS;
    float max_jump_h = clampf((float)(state->ground_y - 6) * 0.28f, 3.5f, 7.5f);
    jump_offset_y = (int)(4.0f * t * (1.0f - t) * max_jump_h + 0.5f);

    int j_frame = (t < 0.30f) ? 0 : ((t < 0.72f) ? 1 : 2);
    sprite = DOG_JUMP_FRAMES[j_frame];
  } else {
    int r_frame = (int)((state->tick / DOG_ANIM_CADENCE) % DOG_FRAME_COUNT);
    sprite = DOG_RUN_FRAMES[r_frame];
  }

  int dog_top_y = state->ground_y - DOG_HEIGHT - jump_offset_y;
  if (dog_top_y < 3)
    dog_top_y = 3;

  ColorRGB dog_outline = rgb(150, 255, 170);
  ColorRGB dog_head = rgb(190, 255, 205);
  ColorRGB dog_fill = rgb(6, 14, 9);

  for (int row = 0; row < DOG_HEIGHT; row++) {
    const char *line = sprite[row];
    int first_col = -1, last_col = -1;
    for (int col = 0; line[col] != '\0'; col++) {
      if (line[col] != ' ') {
        if (first_col < 0)
          first_col = col;
        last_col = col;
      }
    }
    if (first_col < 0)
      continue;

    int draw_y = dog_top_y + row;
    if (draw_y < 3 || draw_y >= state->ground_y)
      continue;

    for (int col = first_col; col <= last_col; col++) {
      int draw_x = dog_x + col;
      if (draw_x <= 1 || draw_x >= state->term_width - 2)
        continue;

      char ch = line[col];
      if (ch == ' ') {
        // Solid dark silhouette interior occludes scrolling ruins/bushes behind dog
        set_cell_ascii(draw_y, draw_x, ' ', dog_fill, dog_fill);
      } else {
        ColorRGB fg = (col >= 13 && row <= 2) ? dog_head : dog_outline;
        set_cell_ascii(draw_y, draw_x, ch, fg, dog_fill);
      }
    }
  }

  // Sub-pixel Braille paw dust kicked up on the valley turf
  int dust_y = state->ground_y - 1;
  for (int d = 0; d < 4; d++) {
    int dx = dog_x - 1 - d * 2 -
             (int)((state->tick / 2UL + (unsigned long)d) % 3UL);
    if (dx > cliff_edge_x && dx < state->term_width - 2) {
      uint8_t dust_mask =
          (uint8_t)(0x40U | ((state->tick + (unsigned long)d * 11UL) & 0xA4U));
      char braille[5];
      encode_braille_utf8(dust_mask, braille);
      ColorRGB dust_col =
          lerp_rgb(rgb(110, 230, 130), rgb(40, 100, 55), (float)d / 3.0f);
      set_cell_utf8_fg(dust_y, dx, braille, dust_col);
    }
  }
}

static void draw_string_fg(int y, int x, const char *str, ColorRGB fg,
                           int max_x) {
  for (int i = 0; str[i] != '\0'; i++) {
    int dx = x + i;
    if (dx > 1 && dx < max_x - 2) {
      set_cell_fg(y, dx, str[i], fg);
    }
  }
}

// ============================================================================
// LEVEL 2 RENDERER: "DEEP IN THE WATER" (Multi-Color Aquatic & Bioluminescent)
// ============================================================================

static void draw_cavern_ceiling_and_mist(int term_width, int lake_y,
                                         unsigned long tick) {
  int inner_w = term_width - 4;
  if (inner_w <= 0)
    return;

  int center_x = term_width / 2;

  // 1. Shimmering nocturnal cavern mist particles (violet-blue to aqua)
  for (int i = 0; i < 28; i++) {
    int mist_y = 4 + ((i * 7) % (lake_y - 5 > 1 ? lake_y - 5 : 1));
    int mist_x =
        2 + (((i * 31 + 9) + (int)(tick / (unsigned long)(14 + (i % 3) * 6))) %
             inner_w);
    float pulse = 0.60f + 0.40f * sinf((float)tick * 0.06f + (float)i * 1.4f);
    ColorRGB base_mist = (i % 2 == 0) ? rgb(55, 175, 235) : rgb(95, 115, 225);
    ColorRGB mist_col = {
        (uint8_t)((float)base_mist.r * pulse),
        (uint8_t)((float)base_mist.g * pulse),
        (uint8_t)((float)base_mist.b * pulse),
    };
    set_cell_fg(mist_y, mist_x, (i % 3 == 0) ? ':' : '.', mist_col);
  }

  // 2. Overhanging slate-indigo cavern ceiling & central wet drip stalactite
  for (int x = 2; x < term_width - 2; x++) {
    int dist_center = abs(x - center_x);
    float n = value_noise2d((float)x * 0.25f, 1.5f);
    int stal_len = 1 + (int)(n * 2.8f);
    if (dist_center <= 3) {
      stal_len = 4 - dist_center;
    }

    for (int dy = 0; dy < stal_len; dy++) {
      int y = 3 + dy;
      if (y >= lake_y - 4)
        break;
      char ch = (dy == stal_len - 1) ? ((dist_center == 0) ? 'V' : 'v')
                                     : ((x % 2 == 0) ? '#' : '|');
      ColorRGB fg = (dist_center <= 2 && dy == stal_len - 1)
                        ? rgb(195, 248, 255)
                        : lerp_rgb(rgb(55, 95, 145), rgb(75, 155, 205), n);
      ColorRGB bg = rgb(10, 20, 38);
      set_cell_ascii(y, x, ch, fg, bg);
    }
  }
}

// Leaping fish trajectory, derived from the current terminal size every frame.
// (Plain struct returned by value — like a small Java record, but on the stack.)
typedef struct {
  int launch_x; // left column of the fish sprite when it leaves the water
  int span;     // horizontal distance travelled while airborne
  int peak;     // apex height in rows above the waterline
} FishPath;

static FishPath fish_path(int term_width, int lake_y) {
  FishPath p;
  int cx = term_width / 2;
  p.span = clampi(term_width / 4, 10, 28);
  p.launch_x = cx + 11; // right of the central drop & splash crown
  if (p.launch_x + p.span + FISH_WIDTH > term_width - 3) {
    p.launch_x = term_width - 3 - FISH_WIDTH - p.span;
  }
  p.launch_x = clampi(p.launch_x, 3, term_width - 3);
  p.peak = clampi(lake_y - 8, 3, 10); // keep clear of HUD & stalactites
  return p;
}

static void draw_lake_and_ripples(const GameState *state, int lake_y,
                                  int impact_y, int cycle_tick,
                                  int cycle_len) {
  int w = state->term_width;
  int h = state->term_height;
  int cx = w / 2;
  int lake_h = (h - 2) - lake_y + 1;
  if (lake_h < 4)
    return;

  const int impact_frame = 52;
  int ripple_age = (cycle_tick >= impact_frame)
                       ? (cycle_tick - impact_frame)
                       : (cycle_tick + cycle_len - impact_frame);

  int manual_age = (state->jump_ticks > 0)
                       ? (JUMP_DURATION_TICKS - state->jump_ticks)
                       : -1;

  // Leaping fish ripples: one ring where it leaves, one where it dives back in
  FishPath fp = fish_path(w, lake_y);
  int fish_age = (state->fish_ticks > 0)
                     ? (FISH_JUMP_DURATION_TICKS - state->fish_ticks)
                     : -1;
  int fish_launch_cx = fp.launch_x + FISH_WIDTH / 2;
  int fish_land_cx = fp.launch_x + fp.span + FISH_WIDTH / 2;

  // 1. Render Multi-Color Lake Surface & Deep Sapphire/Indigo Water Strata
  for (int y = lake_y; y <= h - 2; y++) {
    float depth_t = (float)(y - lake_y) / (float)(lake_h > 1 ? lake_h - 1 : 1);

    // Surface turquoise-blue -> mid sapphire -> deep abyssal navy
    ColorRGB base_bg =
        lerp_rgb(rgb(12, 45, 78), rgb(4, 11, 24), powf(depth_t, 0.72f));
    ColorRGB wave_fg =
        lerp_rgb(rgb(95, 220, 255), rgb(28, 82, 145), depth_t);

    for (int x = 2; x < w - 2; x++) {
      float dx = ((float)x - (float)cx) * 0.42f;
      float dy = ((float)y - (float)impact_y) * 1.35f;
      float dist = sqrtf(dx * dx + dy * dy);

      float r1 = (float)ripple_age * 0.52f;
      float r2 = (float)ripple_age * 0.34f - 2.2f;
      float r3 = (float)ripple_age * 0.18f - 4.0f;
      float fade = clampf(1.0f - (float)ripple_age / 58.0f, 0.0f, 1.0f);

      bool on_ring1 = (fabsf(dist - r1) < 0.72f) && (fade > 0.05f);
      bool on_ring2 =
          (r2 > 0.5f) && (fabsf(dist - r2) < 0.65f) && (fade > 0.12f);
      bool on_ring3 =
          (r3 > 0.5f) && (fabsf(dist - r3) < 0.55f) && (fade > 0.20f);

      bool on_manual = false;
      if (manual_age >= 0) {
        float rm = (float)manual_age * 0.65f;
        on_manual = (fabsf(dist - rm) < 0.75f);
      }

      if (fish_age >= 0 && !on_manual) {
        // Launch ring expands while the fish is airborne
        float lx = ((float)x - (float)fish_launch_cx) * 0.42f;
        float ly = ((float)y - (float)(lake_y + 1)) * 1.35f;
        float ld = sqrtf(lx * lx + ly * ly);
        float lr = (float)fish_age * 0.40f;
        if (fish_age < FISH_AIR_TICKS - 8 && fabsf(ld - lr) < 0.7f) {
          on_manual = true;
        }
        // Re-entry ring starts when the fish hits the water
        int land_age = fish_age - FISH_AIR_TICKS;
        if (land_age >= 0) {
          float ex = ((float)x - (float)fish_land_cx) * 0.42f;
          float ed = sqrtf(ex * ex + ly * ly);
          if (fabsf(ed - (float)land_age * 0.45f) < 0.7f) {
            on_manual = true;
          }
        }
      }

      if (y == lake_y) {
        // Shimmering cyan-turquoise horizon waterline
        char lch = ((x + (int)(state->tick / 4UL)) % 4 == 0) ? '~' : '=';
        ColorRGB h_fg = rgb(160, 245, 255);
        set_cell_ascii(y, x, lch, h_fg, rgb(18, 62, 102));
      } else if (on_ring1 || on_ring2 || on_ring3 || on_manual) {
        // Bioluminescent ice-cyan & azure expanding ripple crests
        float ring_intensity =
            on_ring1 ? fade : (on_manual ? 0.95f : fade * 0.78f);
        ColorRGB r_fg =
            lerp_rgb(wave_fg, rgb(220, 252, 255), ring_intensity);
        ColorRGB r_bg =
            lerp_rgb(base_bg, rgb(26, 88, 145), ring_intensity * 0.75f);

        char r_ch;
        if (fabsf(dy) < 0.9f) {
          r_ch = (dx < 0.0f) ? '(' : ')';
        } else if (on_ring1 || on_manual) {
          r_ch = '~';
        } else {
          r_ch = '-';
        }
        set_cell_ascii(y, x, r_ch, r_fg, r_bg);
      } else {
        // Ambient horizontal lake currents & underwater caustic shimmer
        int wave_shift = (int)(state->tick / (unsigned long)(5 + (y % 3) * 2));
        int wx = x + ((y % 2 == 0) ? wave_shift : -wave_shift);
        float n_water = value_noise2d((float)wx * 0.18f, (float)y * 0.65f);

        char w_ch = ' ';
        if (y < lake_y + 4 && (wx % 7 == 0)) {
          w_ch = '~';
        } else if (n_water > 0.72f) {
          w_ch = (y % 2 == 0) ? '~' : '-';
        } else if (n_water > 0.58f && (wx % 11 == 0)) {
          w_ch = '.';
        }
        set_cell_ascii(y, x, w_ch, wave_fg, base_bg);
      }
    }
  }

  // 2. Multi-Color Underwater Flora (Emerald Kelp, Pink/Amber Coral & Aqua Reeds)
  int flora_top = h - 1 - WATER_FLORA_HEIGHT;
  if (flora_top > lake_y + 2) {
    for (int f = 0; f < 5; f++) {
      int fx = 6 + (f * (w - 16)) / 4;
      if (abs(fx - cx) < 12)
        continue;
      int fl_type = f % WATER_FLORA_TYPE_COUNT;
      const char *const *fl_sprite = WATER_FLORA_SPRITES[fl_type];

      ColorRGB fl_hi, fl_lo;
      if (fl_type == 0) {
        // Swaying emerald-teal kelp fronds
        fl_hi = rgb(65, 235, 175);
        fl_lo = rgb(28, 135, 100);
      } else if (fl_type == 1) {
        // Bioluminescent coral reef spire (warm coral-pink & golden polyps)
        fl_hi = rgb(255, 120, 165);
        fl_lo = rgb(175, 65, 110);
      } else {
        // Luminous turquoise-cyan aquatic reeds
        fl_hi = rgb(95, 225, 250);
        fl_lo = rgb(40, 130, 165);
      }

      for (int r = 0; r < WATER_FLORA_HEIGHT; r++) {
        const char *line = fl_sprite[r];
        for (int c = 0; line[c] != '\0'; c++) {
          char ch = line[c];
          int dx = fx + c;
          if (ch != ' ' && dx > 1 && dx < w - 2) {
            ColorRGB fg = (ch == 'o')
                              ? rgb(255, 220, 115)
                              : ((ch == '#' || ch == '^' || ch == '\\' ||
                                  ch == '/')
                                     ? fl_hi
                                     : fl_lo);
            set_cell_fg(flora_top + r, dx, ch, fg);
          }
        }
      }
    }
  }

  // 3. Rising underwater micro-bubbles in shimmering ice-blue Braille
  for (int b = 0; b < 8; b++) {
    int b_span = lake_h - 2;
    if (b_span <= 1)
      break;
    int by = (h - 3) - (int)((state->tick / 4UL + (unsigned long)(b * 7)) %
                             (unsigned long)b_span);
    int bx = 8 + ((b * 29 + (int)(sinf((float)by * 0.5f) * 2.0f)) % (w - 16));
    if (by > lake_y + 1 && by < h - 2) {
      char braille[5];
      encode_braille_utf8((b % 2 == 0) ? 0x18U : 0x24U, braille);
      set_cell_utf8_fg(by, bx, braille, rgb(155, 235, 255));
    }
  }
}

static void draw_falling_drop_and_splash(const GameState *state, int lake_y,
                                         int impact_y, int cycle_tick) {
  int cx = state->term_width / 2;
  int start_y = 6;
  int fall_dist = impact_y - start_y - WATER_DROP_HEIGHT + 2;
  if (fall_dist < 4)
    return;

  // Ice-white specular core, bright cyan body, and deep azure mist wake
  ColorRGB drop_specular = rgb(235, 252, 255);
  ColorRGB drop_core = rgb(95, 215, 255);
  ColorRGB trail_col = rgb(40, 115, 205);

  if (cycle_tick < 16) {
    const char *const *sprite = WATER_DROP_FRAMES[0];
    int left_x = cx - WATER_DROP_WIDTH / 2;
    for (int r = 0; r < WATER_DROP_HEIGHT; r++) {
      for (int c = 0; sprite[r][c] != '\0'; c++) {
        char ch = sprite[r][c];
        if (ch != ' ') {
          set_cell_fg(start_y + r, left_x + c, ch,
                      (ch == 'o') ? drop_specular : drop_core);
        }
      }
    }
  } else if (cycle_tick < 52) {
    float t = (float)(cycle_tick - 16) / 36.0f;
    int drop_y = start_y + (int)(t * t * (float)fall_dist + 0.5f);
    int frame_idx = (t < 0.55f) ? 1 : 2;
    const char *const *sprite = WATER_DROP_FRAMES[frame_idx];
    int left_x = cx - WATER_DROP_WIDTH / 2;

    for (int tr = 1; tr <= 4; tr++) {
      int ty = drop_y - tr;
      if (ty > start_y) {
        char braille[5];
        encode_braille_utf8((tr % 2 == 0) ? 0x12U : 0x24U, braille);
        ColorRGB t_fg = lerp_rgb(drop_core, trail_col, (float)tr / 4.5f);
        set_cell_utf8_fg(ty, cx, braille, t_fg);
      }
    }

    for (int r = 0; r < WATER_DROP_HEIGHT; r++) {
      for (int c = 0; sprite[r][c] != '\0'; c++) {
        char ch = sprite[r][c];
        if (ch != ' ' && (drop_y + r) < impact_y + 2) {
          ColorRGB fg =
              (ch == 'o' || ch == '#') ? drop_specular : drop_core;
          set_cell_fg(drop_y + r, left_x + c, ch, fg);
        }
      }
    }
  } else if (cycle_tick < 76) {
    int splash_elapsed = cycle_tick - 52;
    int s_frame = (splash_elapsed / 6) % WATER_SPLASH_FRAME_COUNT;
    const char *const *sprite = WATER_SPLASH_FRAMES[s_frame];
    int top_y = impact_y - WATER_SPLASH_HEIGHT + 2;
    int left_x = cx - WATER_SPLASH_WIDTH / 2;

    for (int r = 0; r < WATER_SPLASH_HEIGHT; r++) {
      for (int c = 0; sprite[r][c] != '\0'; c++) {
        char ch = sprite[r][c];
        int dx = left_x + c;
        int dy = top_y + r;
        if (ch != ' ' && dx > 1 && dx < state->term_width - 2 && dy > 2) {
          ColorRGB fg = (ch == 'o' || ch == '#' || ch == '|') ? drop_specular
                                                              : drop_core;
          set_cell_fg(dy, dx, ch, fg);
        }
      }
    }

    float st = (float)splash_elapsed / 24.0f;
    for (int d = -2; d <= 2; d++) {
      if (d == 0)
        continue;
      int dx = cx + (int)((float)d * (3.5f + st * 7.0f));
      int dy = (impact_y - 2) -
               (int)((4.0f * st * (1.0f - st)) * (float)(5 - abs(d)));
      if (dx > 2 && dx < state->term_width - 3 && dy > 3 && dy < lake_y + 2) {
        char braille[5];
        encode_braille_utf8(0x18U, braille);
        set_cell_utf8_fg(dy, dx, braille, drop_specular);
      }
    }
  }
}

static void draw_fish_splash(int term_width, int lake_y, int center_x,
                             int frame) {
  const char *const *sprite = FISH_SPLASH_FRAMES[frame];
  int top_y = lake_y - FISH_SPLASH_HEIGHT + 1;
  int left_x = center_x - FISH_SPLASH_WIDTH / 2;
  for (int r = 0; r < FISH_SPLASH_HEIGHT; r++) {
    for (int c = 0; sprite[r][c] != '\0'; c++) {
      char ch = sprite[r][c];
      int dx = left_x + c;
      if (ch != ' ' && dx > 1 && dx < term_width - 2 && top_y + r > 2) {
        ColorRGB fg = (ch == 'o' || ch == '|') ? rgb(235, 252, 255)
                                               : rgb(95, 215, 255);
        set_cell_fg(top_y + r, dx, ch, fg);
      }
    }
  }
}

// Fish position along its parabolic arc for u in [0,1]; returns sprite top row
static int fish_top_y(int lake_y, const FishPath *fp, float u) {
  float height = 4.0f * u * (1.0f - u) * (float)fp->peak;
  // At u=0/1 the sprite top sits on the waterline, so it's fully submerged
  return lake_y - (int)(height + 0.5f);
}

static void draw_leaping_fish(const GameState *state, int lake_y) {
  if (state->fish_ticks <= 0)
    return;

  int w = state->term_width;
  FishPath fp = fish_path(w, lake_y);
  int age = FISH_JUMP_DURATION_TICKS - state->fish_ticks;
  int launch_cx = fp.launch_x + FISH_WIDTH / 2;
  int land_cx = fp.launch_x + fp.span + FISH_WIDTH / 2;

  // Launch burst: first 12 ticks at the take-off point
  if (age < 12) {
    draw_fish_splash(w, lake_y, launch_cx, age / 4);
  }

  if (age < FISH_AIR_TICKS) {
    float u = (float)age / (float)FISH_AIR_TICKS;
    int top_y = fish_top_y(lake_y, &fp, u);
    int left_x = fp.launch_x + (int)(u * (float)fp.span + 0.5f);
    int frame = (u < 0.38f) ? 0 : ((u < 0.62f) ? 1 : 2);
    const char *const *sprite = FISH_FRAMES[frame];

    // Sparkling water droplets shed along the arc behind the tail
    for (int t = 1; t <= 3; t++) {
      float ut = u - (float)t * 0.05f;
      if (ut <= 0.0f)
        break;
      int ty = fish_top_y(lake_y, &fp, ut) + 1;
      int tx = fp.launch_x + (int)(ut * (float)fp.span + 0.5f);
      if (ty < lake_y && ty > 2) {
        char braille[5];
        encode_braille_utf8((t % 2 == 0) ? 0x12U : 0x24U, braille);
        set_cell_utf8_fg(ty, tx, braille,
                         lerp_rgb(rgb(200, 245, 255), rgb(40, 115, 205),
                                  (float)t / 3.5f));
      }
    }

    // Bioluminescent koi: coral-orange scales, golden fins, white eye
    for (int r = 0; r < FISH_HEIGHT; r++) {
      for (int c = 0; sprite[r][c] != '\0'; c++) {
        char ch = sprite[r][c];
        int dy = top_y + r;
        int dx = left_x + c;
        // Only rows above the waterline are visible (fish emerges/submerges)
        if (ch == ' ' || dy >= lake_y || dy <= 2 || dx <= 1 || dx >= w - 2)
          continue;
        ColorRGB fg;
        if (ch == 'o') {
          fg = rgb(255, 255, 255);
        } else if (ch == '(') {
          fg = rgb(255, 140, 80);
        } else if (ch == '<' || ch == '>' || ch == 'v') {
          fg = rgb(255, 210, 95);
        } else {
          fg = rgb(255, 185, 120);
        }
        set_cell_fg(dy, dx, ch, fg);
      }
    }
  } else {
    // Re-entry splash at the landing point
    int land_age = age - FISH_AIR_TICKS;
    if (land_age < 18) {
      draw_fish_splash(w, lake_y, land_cx, land_age / 6);
    }
  }
}

static void render_level_moonlit_valley(const GameState *state) {
  int max_cliff_x = (state->term_width * 34) / 100;
  if (max_cliff_x < 18)
    max_cliff_x = 18;
  int cliff_edge_x = clampi((state->term_width * 27) / 100, 16, max_cliff_x);

  int min_cliff_y = 3 + CLIFF_BOY_HEIGHT; // keeps watcher below HUD row 2
  int max_cliff_y = state->ground_y - 4;  // keeps cliff taller than valley floor
  if (max_cliff_y < min_cliff_y)
    max_cliff_y = min_cliff_y;
  int cliff_top_y =
      clampi((state->term_height * 48) / 100, min_cliff_y, max_cliff_y);

  draw_sky_stars(state->term_width, state->ground_y, state->tick);
  draw_clouds_background(state->term_width, state->ground_y, state->tick);
  draw_planet_shader(state->term_width, state->ground_y, state->tick);
  draw_clouds_foreground(state->term_width, state->ground_y, state->tick);
  draw_valley_and_props(state->term_width, state->ground_y, cliff_edge_x,
                        state->tick);
  draw_cliff_and_watcher(state, cliff_top_y, cliff_edge_x);
  draw_dog(state, cliff_edge_x);
}

static void render_level_deep_in_the_water(const GameState *state) {
  int lake_y = clampi((state->term_height * 54) / 100, 12,
                      state->term_height - 8);
  int impact_y = lake_y + (state->term_height - 2 - lake_y) / 3;

  const int cycle_len = 96; // 1.6s complete drop -> splash -> ripple cycle
  int cycle_tick = (int)(state->tick % (unsigned long)cycle_len);

  draw_cavern_ceiling_and_mist(state->term_width, lake_y, state->tick);
  draw_lake_and_ripples(state, lake_y, impact_y, cycle_tick, cycle_len);
  draw_falling_drop_and_splash(state, lake_y, impact_y, cycle_tick);
  draw_leaping_fish(state, lake_y);
}

// ============================================================================
// LEVEL 3 RENDERER: "MEDIEVAL VICE" (Knight Enforcer in Aviator Shades)
// Palette: Forged Steel-Blue (left fill/rim) + Torchlight Amber-Gold (right key)
// over an Obsidian Void background with a tight two-tone silhouette aura.
// ============================================================================

typedef enum {
  VM_BG = 0,
  VM_CAP,
  VM_RIDGE,
  VM_RIVET,
  VM_GOLD,
  VM_VISOR,
  VM_LENS,
  VM_FRAME,
  VM_SKIN,
  VM_STUBBLE,
  VM_MUSTACHE,
  VM_CHAINMAIL,
  VM_COLLAR
} ViceMaterial;

// Per-cell shading context (stack-allocated value struct)
typedef struct {
  int x, y;           // terminal cell
  int sr, sc;         // sprite row / col
  float sxf, syf;     // continuous sprite coordinates
  float key;          // 0 = left (forged steel-blue) .. 1 = right (torch gold)
  float edge_dist;    // signed horizontal distance from silhouette edge
  unsigned long tick;
  int jump_ticks;
} ViceCtx;

typedef struct {
  uint32_t cp; // Unicode codepoint (ASCII or Braille U+2800..U+28FF)
  ColorRGB fg;
  ColorRGB bg;
} ViceCell;

static const ColorRGB VICE_STEEL_BLUE = {115, 190, 250};
static const ColorRGB VICE_STEEL_ICE = {205, 238, 255};
static const ColorRGB VICE_TORCH_GOLD = {255, 185, 52};
static const ColorRGB VICE_TORCH_BRIGHT = {255, 240, 145};

static inline ColorRGB scale_rgb(ColorRGB c, float k) {
  k = clampf(k, 0.0f, 1.0f);
  ColorRGB out = {(uint8_t)((float)c.r * k), (uint8_t)((float)c.g * k),
                  (uint8_t)((float)c.b * k)};
  return out;
}

static inline char vice_sprite_at(int sr, int sc) {
  if (sr < 0 || sr >= VICE_COP_ROWS || sc < 0 || sc >= VICE_COP_COLS)
    return ' ';
  return VICE_COP_SPRITE[sr][sc];
}

static inline bool vice_is_outline_char(char c) {
  return c == '/' || c == '\\' || c == '|' || c == '(' || c == ')' ||
         c == '_' || c == '-' || c == '.' || c == ',' || c == '`' ||
         c == '\'';
}

// Sprite rows 12..15 between the two frames hold the aviator lenses
static inline bool vice_in_lens_context(int sr, int sc) {
  if (sr < 12 || sr > 15)
    return false;
  return vice_sprite_at(sr, sc - 1) == '~' ||
         vice_sprite_at(sr, sc + 1) == '~' ||
         vice_sprite_at(sr, sc - 2) == '~' || vice_sprite_at(sr, sc + 2) == '~';
}

// Material used to fill the "inside" of a row (empty interior / off-line area)
static inline ViceMaterial vice_zone_fill(int sr, int sc) {
  if (vice_in_lens_context(sr, sc))
    return VM_LENS;
  if (sr <= 7)
    return VM_CAP;
  if (sr <= 10)
    return VM_VISOR;
  if (sr <= 19 && (sc <= 14 || sc >= 41))
    return VM_CAP; // hinged cheek guards on left and right of face
  if (sr <= 22)
    return VM_SKIN;
  if (sr >= 23 && sc >= 16 && sc <= 39)
    return VM_CHAINMAIL;
  return VM_COLLAR;
}

static ViceMaterial vice_material_of(char c, int sr, int sc) {
  switch (c) {
  case '#':
    return VM_CAP;
  case '!':
    return VM_RIDGE;
  case 'O':
    return VM_RIVET;
  case '$':
  case '*':
  case '@':
    return VM_GOLD;
  case '=':
    if (sr <= 10)
      return VM_VISOR;
    if (sr >= 26)
      return VM_COLLAR;
    return (sr >= 19) ? VM_MUSTACHE : VM_FRAME;
  case '~':
    return VM_LENS;
  case '8':
  case 'd':
  case 'b':
  case 'Y':
  case 'P':
    return (sr >= 19) ? VM_MUSTACHE : VM_FRAME;
  case 'M':
  case 'W':
    return (sr <= 22) ? VM_MUSTACHE : VM_COLLAR;
  case ':':
    return VM_STUBBLE;
  case '%':
    return VM_CHAINMAIL;
  case 'V':
    return VM_COLLAR;
  default:
    return vice_zone_fill(sr, sc);
  }
}

// Obsidian void background with dual-tone silhouette aura (steel-blue left,
// flickering torchlight amber-gold right) matching the reference portrait
static ViceCell vice_shade_background(const ViceCtx *ctx, int term_w,
                                      int term_h) {
  (void)term_w;
  (void)term_h;
  ViceCell out;
  ColorRGB bg = rgb(5, 7, 12);

  // Subtle CRT scanline darkening on odd rows
  if (ctx->y % 2 == 1)
    bg = scale_rgb(bg, 0.84f);

  // Flickering torchlight pulse + Space action boost
  float flicker =
      0.88f + 0.12f * sinf((float)ctx->tick * 0.22f + (float)ctx->y * 0.3f);
  float boost = (ctx->jump_ticks > 0) ? 1.65f : 1.0f;

  // Tight halo aura around the helmet & pauldron silhouette
  if (ctx->edge_dist > 0.0f && ctx->edge_dist < 8.5f) {
    float aura = expf(-ctx->edge_dist * 0.52f) * 0.58f * flicker * boost;
    ColorRGB halo_col =
        (ctx->sxf < 28.0f) ? VICE_STEEL_BLUE : VICE_TORCH_GOLD;
    bg = lerp_rgb(bg, halo_col, clampf(aura, 0.0f, 0.80f));

    // Soft Braille dithered outer glow closest to the armor rim
    if (ctx->edge_dist < 2.6f) {
      uint8_t mask = 0;
      float density = (1.0f - ctx->edge_dist / 2.6f) * 0.55f * boost;
      for (int bit = 0; bit < 8; bit++) {
        if (hash2d(ctx->x * 7 + bit, ctx->y * 13 + bit) < density)
          mask |= (uint8_t)(1U << bit);
      }
      if (mask != 0) {
        out.cp = 0x2800U + mask;
        out.fg = scale_rgb(halo_col, 0.65f);
        out.bg = bg;
        return out;
      }
    }
  }

  out.cp = ' ';
  out.fg = bg;
  out.bg = bg;
  return out;
}

static ViceCell vice_shade_material(ViceMaterial mat, char src,
                                    const ViceCtx *ctx, int term_w,
                                    int term_h) {
  ViceCell out = {' ', VICE_STEEL_BLUE, rgb(8, 11, 18)};
  float n = value_noise2d(ctx->sxf * 1.7f, ctx->syf * 2.3f);

  // Flickering torchlight modulation on the right key light
  float flicker = 0.92f + 0.08f * sinf((float)ctx->tick * 0.25f);
  if (ctx->jump_ticks > 0) {
    flicker = 1.25f;
  }

  // Smooth transition from cold forged steel-blue (left) to warm torch gold (right)
  float k_curve = clampf((ctx->key - 0.12f) / 0.76f, 0.0f, 1.0f);
  ColorRGB armor_tint = lerp_rgb(VICE_STEEL_BLUE, VICE_TORCH_GOLD, k_curve);

  switch (mat) {
  case VM_BG:
    return vice_shade_background(ctx, term_w, term_h);

  case VM_CAP:
  case VM_RIDGE: {
    // 3D spherical/cylindrical normal for the forged sallet helmet dome & cheek guards
    float nx = clampf((ctx->sxf - 28.0f) / 16.5f, -1.0f, 1.0f);
    float ny = clampf((ctx->syf - 5.5f) / 6.5f, -1.0f, 1.0f);
    float nz = sqrtf(clampf(1.0f - nx * nx * 0.75f - ny * ny * 0.35f, 0.05f, 1.0f));

    // Dual light: steel-blue rim on left + blazing torchlight hotspot on upper-right dome
    float rim_left = clampf(-nx * 0.85f + (1.0f - nz) * 0.45f, 0.0f, 1.0f);
    float key_right =
        clampf(nx * 0.65f - ny * 0.30f + nz * 0.55f, 0.0f, 1.0f) * flicker;
    float spec_dx = nx - 0.42f;
    float spec_dy = ny + 0.28f;
    float spec = expf(-(spec_dx * spec_dx * 9.5f + spec_dy * spec_dy * 8.5f)) *
                 flicker;

    float lum = clampf(0.18f + 0.48f * rim_left + 0.68f * key_right +
                           0.55f * spec + 0.08f * (n - 0.5f),
                       0.06f, 1.0f);
    if (mat == VM_RIDGE) {
      lum = clampf(lum + 0.16f, 0.15f, 1.0f);
    }

    ColorRGB base_col =
        (nx < -0.05f)
            ? lerp_rgb(rgb(42, 78, 118), VICE_STEEL_ICE, clampf(lum * 1.1f, 0.0f, 1.0f))
            : lerp_rgb(rgb(68, 52, 32), VICE_TORCH_BRIGHT,
                       clampf(lum * 1.05f, 0.0f, 1.0f));
    if (spec > 0.35f) {
      base_col = lerp_rgb(base_col, rgb(255, 250, 210), (spec - 0.35f) / 0.65f);
    }

    // Use Braille sub-pixel dithering for smooth forged metal + '@'/'#' on blazing hotspot
    if (spec > 0.55f) {
      out.cp = '@';
    } else {
      uint8_t mask = 0;
      for (int bit = 0; bit < 8; bit++) {
        if (hash2d(ctx->x * 8 + bit, ctx->y * 11 + bit * 3) < lum * 0.90f)
          mask |= (uint8_t)(1U << bit);
      }
      out.cp = 0x2800U + mask;
    }
    out.fg = base_col;
    out.bg = scale_rgb(base_col, 0.22f + 0.18f * lum);
    break;
  }

  case VM_RIVET: {
    // Raised forged steel/brass rivet stud ('O')
    out.cp = 'O';
    out.fg = lerp_rgb(VICE_STEEL_ICE, VICE_TORCH_BRIGHT, k_curve);
    out.bg = scale_rgb(armor_tint, 0.28f);
    break;
  }

  case VM_GOLD: {
    // Glowing Heraldic Shield Badge on Helmet Brow ('*' border, '$' shield, '@' blazing core)
    float dx = (ctx->sxf - 27.5f) / 4.5f;
    float dy = (ctx->syf - 5.2f) / 2.8f;
    float radial = clampf(1.0f - sqrtf(dx * dx + dy * dy) * 0.65f, 0.0f, 1.0f);
    float pulse = 0.85f + 0.15f * sinf((float)ctx->tick * 0.12f);
    if (ctx->jump_ticks > 0)
      pulse = 1.3f;

    if (src == '@') {
      out.cp = '@';
      out.fg = rgb(255, 252, 225);
      out.bg = rgb(195, 138, 32);
    } else if (src == '*') {
      out.cp = '*';
      out.fg = lerp_rgb(rgb(185, 125, 28), VICE_TORCH_BRIGHT, radial * pulse);
      out.bg = rgb(58, 36, 10);
    } else {
      out.cp = '$';
      out.fg = lerp_rgb(VICE_TORCH_GOLD, rgb(255, 250, 200),
                        clampf(radial * pulse, 0.0f, 1.0f));
      out.bg = rgb(95, 62, 14);
    }
    break;
  }

  case VM_VISOR: {
    // Riveted horizontal steel brow band across the helmet
    float band_hi = clampf(1.0f - fabsf(ctx->syf - 9.0f) / 1.4f, 0.0f, 1.0f);
    out.cp = '=';
    out.fg = lerp_rgb(scale_rgb(armor_tint, 0.65f),
                      lerp_rgb(VICE_STEEL_ICE, VICE_TORCH_BRIGHT, k_curve),
                      band_hi);
    out.bg = scale_rgb(armor_tint, 0.20f);
    break;
  }

  case VM_LENS: {
    // Mirrored Aviator Lenses reflecting warm flickering torchlight bars
    float t = clampf((ctx->syf - 12.0f) / 4.0f, 0.0f, 1.0f);
    ColorRGB glass_base =
        lerp_rgb(rgb(55, 32, 16), rgb(185, 105, 28), (1.0f - t * 0.5f));

    // Diagonal torchlight reflection streaks inside each lens
    float local_x = (ctx->sxf < 28.0f) ? (ctx->sxf - 16.0f) : (ctx->sxf - 31.0f);
    float diag = local_x + (ctx->syf - 12.0f) * 1.65f;
    float streak1 = expf(-powf(diag - 5.2f, 2.0f) * 0.75f);
    float streak2 = expf(-powf(diag - 8.4f, 2.0f) * 1.10f) * 0.75f;
    float glint = clampf((streak1 + streak2) * flicker, 0.0f, 1.0f);

    if (ctx->jump_ticks > 0) {
      float f = (float)ctx->jump_ticks / (float)JUMP_DURATION_TICKS;
      glint = clampf(glint + f * 0.85f, 0.0f, 1.0f);
    }

    out.cp = (glint > 0.52f) ? '/' : ((ctx->y % 2 == 0) ? '~' : '-');
    out.fg = lerp_rgb(glass_base, VICE_TORCH_BRIGHT, glint);
    out.bg = lerp_rgb(rgb(18, 12, 10), rgb(115, 68, 18), glint * 0.65f);
    break;
  }

  case VM_FRAME: {
    float shade = 0.65f + 0.35f * n;
    out.cp = (uint32_t)(unsigned char)((src == '=') ? '=' : '8');
    if (src == 'd' || src == 'b' || src == 'Y' || src == 'P')
      out.cp = (uint32_t)(unsigned char)src;
    out.fg = scale_rgb(lerp_rgb(VICE_STEEL_ICE, VICE_TORCH_BRIGHT, k_curve),
                       shade);
    out.bg = rgb(28, 20, 14);
    break;
  }

  case VM_SKIN:
  case VM_STUBBLE: {
    // Cylindrical Lambert lighting on the knight's rugged face & cleft chin
    float nx = clampf((ctx->sxf - 27.5f) / 14.0f, -1.0f, 1.0f);
    float nz = sqrtf(clampf(1.0f - nx * nx, 0.05f, 1.0f));
    float lambert =
        clampf(nx * 0.50f + nz * 0.82f, 0.0f, 1.0f) * (0.85f + 0.15f * flicker);
    float b = clampf(0.16f + 0.68f * lambert + 0.12f * (n - 0.5f), 0.05f, 1.0f);

    // Warm bronze skin on the torchlit right, cool slate-blue shadow on the left
    ColorRGB skin_col =
        lerp_rgb(rgb(95, 130, 165), rgb(245, 178, 95), clampf(ctx->key * 1.15f, 0.0f, 1.0f));
    ColorRGB bg = scale_rgb(skin_col, 0.12f + 0.15f * b);

    if (mat == VM_STUBBLE) {
      float h = hash2d(ctx->x * 3, ctx->y * 5);
      out.cp = (h > 0.66f) ? ':' : (h > 0.33f ? '.' : ';');
      out.fg = scale_rgb(skin_col, 0.42f + 0.45f * b);
    } else {
      uint8_t mask = 0;
      for (int bit = 0; bit < 8; bit++) {
        if (hash2d(ctx->x * 8 + bit, ctx->y * 11 + bit * 3) < b * 0.86f)
          mask |= (uint8_t)(1U << bit);
      }
      out.cp = 0x2800U + mask;
      out.fg = scale_rgb(skin_col, 0.48f + 0.52f * b);
    }
    out.bg = bg;
    break;
  }

  case VM_MUSTACHE: {
    // Dark sculpted handlebar captain mustache with subtle rim highlight
    float shade = 0.35f + 0.55f * n + 0.20f * k_curve;
    out.cp = (uint32_t)(unsigned char)((src == 'M' || src == 'W' || src == 'd' ||
                                        src == 'b' || src == '=')
                                           ? src
                                           : 'M');
    out.fg = lerp_rgb(rgb(38, 32, 36), scale_rgb(armor_tint, 0.75f), shade * 0.6f);
    out.bg = rgb(14, 11, 12);
    break;
  }

  case VM_CHAINMAIL: {
    // Interlocking ASCII chainmail gorget (':' / '%' / '8' / '#') on throat & straps
    static const char mail_glyphs[] = ":%8#;:";
    int g_idx = (int)((unsigned int)(ctx->x * 3 + ctx->y * 5) % 6U);
    float nx = clampf((ctx->sxf - 27.5f) / 18.0f, -1.0f, 1.0f);
    float nz = sqrtf(clampf(1.0f - nx * nx, 0.1f, 1.0f));
    float shade = clampf(0.25f + 0.55f * nz + 0.25f * nx, 0.12f, 1.0f);

    out.cp = (uint32_t)(unsigned char)mail_glyphs[g_idx];
    out.fg = scale_rgb(armor_tint, 0.38f + 0.58f * shade);
    out.bg = rgb(9, 11, 16);
    break;
  }

  case VM_COLLAR: {
    // Upturned Bevor Armor Collar & Layered Forged Steel Shoulder Pauldrons
    float fold = 0.5f + 0.5f * cosf((ctx->sxf - 27.5f) * 0.35f);
    float shade =
        clampf(0.28f + 0.45f * fold + 0.27f * (n - 0.3f), 0.10f, 1.0f) *
        flicker;
    ColorRGB plate_col =
        (ctx->sxf < 27.5f)
            ? lerp_rgb(rgb(48, 88, 135), VICE_STEEL_ICE, shade)
            : lerp_rgb(rgb(92, 62, 24), VICE_TORCH_BRIGHT, shade);

    uint8_t mask = 0;
    for (int bit = 0; bit < 8; bit++) {
      if (hash2d(ctx->x * 6 + bit, ctx->y * 9 + bit * 5) < shade * 0.88f)
        mask |= (uint8_t)(1U << bit);
    }
    out.cp = 0x2800U + mask;
    out.fg = plate_col;
    out.bg = scale_rgb(plate_col, 0.22f + 0.14f * shade);
    break;
  }
  }
  return out;
}

// Structural priority of a sprite glyph for box-filter supersampling on compact
// MacBook viewports (ensures 1-row details like aviator rims, rivets, badge,
// and brow bands are never dropped when ch < 1.35).
static int vice_char_priority(char c, int sr) {
  switch (c) {
  case 'O':
  case '@':
  case '*':
    return 7; // Rivets & royal badge emblem/border
  case '$':
    return 6; // Golden shield fill
  case '8':
  case 'd':
  case 'b':
  case 'Y':
  case 'P':
    return 6; // Aviator sunglasses frame & mustache tips
  case '=':
    return (sr == 8 || sr == 11 || sr == 21 || sr == 27) ? 6 : 4;
  case '!':
  case 'M':
  case 'W':
    return 5; // Helmet central ridge & handlebar mustache
  case '/':
  case '\\':
  case '|':
  case '_':
  case '-':
  case '(':
  case ')':
    return 4; // Crisp armor/lens outline strokes & lens glints
  case '%':
  case ':':
    return 3; // Chainmail gorget & jaw stubble
  case '~':
  case '#':
  case 'V':
    return 2; // Lens, helmet, and pauldron body fills
  case '.':
  case ',':
  case '`':
  case '\'':
    return 1;
  default:
    return 0;
  }
}

// Re-rasterises an outline glyph inside its upscaled block.
// fu/fv: fractional position inside the sprite cell, cw/ch: cells per sprite
// cell. Returns true when this terminal cell lies on the stroke; *side
// tells which half lies "before" the stroke (-1) or "after" it (+1).
static bool vice_outline_hit(char c, float fu, float fv, float cw, float ch,
                             int *side) {
  // At ~1:1 compact MacBook resolution, each sprite cell maps directly to ~1
  // terminal cell, so outline glyphs hit directly without sub-cell clipping.
  if (cw < 1.35f && ch < 1.35f) {
    if (c == '/') {
      *side = ((fu + fv) < 1.0f) ? -1 : 1;
    } else if (c == '\\') {
      *side = (fu < fv) ? -1 : 1;
    } else if (c == '|' || c == '(' || c == ')') {
      *side = (fu < 0.5f) ? -1 : 1;
    } else {
      *side = 0;
    }
    return true;
  }

  float tol_x = 0.5f / (cw > 1.0f ? cw : 1.0f) + 0.02f;
  float tol_y = 0.5f / (ch > 1.0f ? ch : 1.0f) + 0.02f;
  switch (c) {
  case '/': {
    float d = (fu + fv) - 1.0f;
    *side = (d < 0.0f) ? -1 : 1; // -1: top-left half, +1: bottom-right half
    return fabsf(d) <= tol_x || fabsf(d) <= tol_y;
  }
  case '\\': {
    float d = fu - fv;
    *side = (d < 0.0f) ? -1 : 1; // -1: bottom-left half, +1: top-right half
    return fabsf(d) <= tol_x || fabsf(d) <= tol_y;
  }
  case '|':
  case '(':
  case ')':
    *side = (fu < 0.5f) ? -1 : 1;
    return fabsf(fu - 0.5f) <= tol_x;
  case '_':
    *side = 0;
    return (1.0f - fv) <= 2.0f * tol_y;
  case '-':
    *side = 0;
    return fabsf(fv - 0.5f) <= tol_y;
  case '.':
  case ',':
    *side = 0;
    return fabsf(fu - 0.5f) <= tol_x && (1.0f - fv) <= 2.0f * tol_y;
  case '`':
  case '\'':
    *side = 0;
    return fabsf(fu - 0.5f) <= tol_y && fv <= 2.0f * tol_y;
  default:
    *side = 0;
    return false;
  }
}

static void render_level_vice(const GameState *state) {
  int w = state->term_width;
  int h = state->term_height;

  // Portrait viewport: everything inside the border, below the 2 HUD rows
  int left = 2, right = w - 3, top = 3, bottom = h - 2;
  int area_w = right - left + 1;
  int area_h = bottom - top + 1;
  if (area_w <= 0 || area_h <= 0)
    return;

  // Per-row horizontal extents of the silhouette (stack arrays, no heap)
  int ext_l[VICE_COP_ROWS];
  int ext_r[VICE_COP_ROWS];
  for (int r = 0; r < VICE_COP_ROWS; r++) {
    ext_l[r] = VICE_COP_COLS;
    ext_r[r] = -1;
    for (int c = 0; c < VICE_COP_COLS; c++) {
      if (VICE_COP_SPRITE[r][c] != ' ') {
        if (c < ext_l[r])
          ext_l[r] = c;
        ext_r[r] = c;
      }
    }
  }

  // Aspect-Ratio Lock: scale proportionally with terminal height and center
  // horizontally so the knight portrait never stretches on widescreen laptops.
  float max_cw = (float)area_w / (float)VICE_COP_COLS;
  float ch = (float)area_h / (float)VICE_COP_ROWS;
  float cw = clampf(ch * 1.18f, 0.85f, max_cw);
  float portrait_w = cw * (float)VICE_COP_COLS;
  float x_origin = (float)left + ((float)area_w - portrait_w) * 0.5f;
  bool low_res_supersample = (ch < 1.35f || cw < 1.35f);

  for (int y = 1; y <= h - 2; y++) {
    for (int x = 2; x <= w - 3; x++) {
      ViceCtx ctx = {x,    y,    -1,   -1,         0.0f,
                     0.0f, 0.5f, 99.0f, state->tick, state->jump_ticks};
      ViceCell cell;

      if (y < top) { // HUD band: plain background
        cell = vice_shade_background(&ctx, w, h);
        tb_set_cell(x, y, cell.cp, rgb_to_tb(cell.fg), rgb_to_tb(cell.bg));
        continue;
      }

      ctx.sxf = ((float)x - x_origin + 0.5f) / cw;
      ctx.syf = ((float)(y - top) + 0.5f) / ch;
      ctx.sr = clampi((int)floorf(ctx.syf), 0, VICE_COP_ROWS - 1);

      // Compute distance outside the knight silhouette for the two-tone aura
      if (ctx.sxf < (float)ext_l[ctx.sr]) {
        ctx.edge_dist = (float)ext_l[ctx.sr] - ctx.sxf;
      } else if (ctx.sxf > (float)(ext_r[ctx.sr] + 1)) {
        ctx.edge_dist = ctx.sxf - (float)(ext_r[ctx.sr] + 1);
      } else {
        ctx.edge_dist = 0.0f;
      }

      // Outside the centered aspect-locked portrait box -> background + aura
      if (ctx.sxf < 0.0f || ctx.sxf >= (float)VICE_COP_COLS) {
        cell = vice_shade_background(&ctx, w, h);
        tb_set_cell(x, y, cell.cp, rgb_to_tb(cell.fg), rgb_to_tb(cell.bg));
        continue;
      }

      ctx.sc = clampi((int)floorf(ctx.sxf), 0, VICE_COP_COLS - 1);

      // Supersampled Box-Filter Priority Selection on compact MacBook screens:
      // inspect all sprite cells covered by this terminal cell [sy0..sy1]x[sx0..sx1]
      // so thin 1-row features (aviator frames, rivets, brow band) are never skipped.
      if (low_res_supersample) {
        int r0 = clampi((int)floorf(((float)(y - top) + 0.05f) / ch), 0,
                        VICE_COP_ROWS - 1);
        int r1 = clampi((int)floorf(((float)(y - top) + 0.95f) / ch), 0,
                        VICE_COP_ROWS - 1);
        int c0 = clampi((int)floorf(((float)x - x_origin + 0.05f) / cw), 0,
                        VICE_COP_COLS - 1);
        int c1 = clampi((int)floorf(((float)x - x_origin + 0.95f) / cw), 0,
                        VICE_COP_COLS - 1);

        int best_prio = vice_char_priority(VICE_COP_SPRITE[ctx.sr][ctx.sc], ctx.sr);
        for (int sr = r0; sr <= r1; sr++) {
          for (int sc = c0; sc <= c1; sc++) {
            int p = vice_char_priority(VICE_COP_SPRITE[sr][sc], sr);
            if (p > best_prio) {
              best_prio = p;
              ctx.sr = sr;
              ctx.sc = sc;
            }
          }
        }
      }

      ctx.key = clampf(ctx.sxf / (float)VICE_COP_COLS, 0.0f, 1.0f);
      float fu = clampf(ctx.sxf - (float)ctx.sc, 0.0f, 0.999f);
      float fv = clampf(ctx.syf - (float)ctx.sr, 0.0f, 0.999f);

      char src = VICE_COP_SPRITE[ctx.sr][ctx.sc];
      bool inside = ctx.sc >= ext_l[ctx.sr] && ctx.sc <= ext_r[ctx.sr];

      if (src == ' ') {
        ViceMaterial m = inside ? vice_zone_fill(ctx.sr, ctx.sc) : VM_BG;
        cell = vice_shade_material(m, src, &ctx, w, h);
      } else if (vice_is_outline_char(src)) {
        int side = 0;
        bool hit = vice_outline_hit(src, fu, fv, cw, ch, &side);
        bool lens = vice_in_lens_context(ctx.sr, ctx.sc);

        // Off-stroke area: inside fill, or background on the silhouette edge
        ViceMaterial fill = vice_zone_fill(ctx.sr, ctx.sc);
        bool is_left_edge = ctx.sc == ext_l[ctx.sr];
        bool is_right_edge = ctx.sc == ext_r[ctx.sr];
        if (!lens && (is_left_edge || is_right_edge)) {
          bool outside = (side == 0) ||
                         (is_left_edge ? (side < 0) : (side > 0));
          if (outside) {
            fill = VM_BG;
            ctx.edge_dist = 0.4f;
          }
        }
        cell = vice_shade_material(fill, ' ', &ctx, w, h);

        if (hit) {
          cell.cp = (uint32_t)(unsigned char)src;
          if (lens && src == '/') {
            cell.fg = rgb(255, 250, 215); // blazing torchlight lens streak
          } else {
            // Dual-tone armor rim: ice-steel on left, torch gold on right
            cell.fg = lerp_rgb(VICE_STEEL_ICE, VICE_TORCH_BRIGHT, ctx.key);
          }
        }
      } else {
        ViceMaterial m = vice_material_of(src, ctx.sr, ctx.sc);
        cell = vice_shade_material(m, src, &ctx, w, h);
      }

      tb_set_cell(x, y, cell.cp, rgb_to_tb(cell.fg), rgb_to_tb(cell.bg));
    }
  }
}

// Multi-Level Dispatch Table with Per-Level Color Palette & Renderer
typedef struct {
  LevelId id;
  const char *name;
  ColorRGB bg_base;
  ColorRGB border_line;
  ColorRGB border_corner;
  ColorRGB border_bg;
  ColorRGB hud_primary;
  ColorRGB hud_title;
  void (*render)(const GameState *state);
} LevelDef;

static const LevelDef LEVEL_TABLE[LEVEL_COUNT] = {
    {
        LEVEL_MOONLIT_VALLEY,
        "MOONLIT VALLEY",
        {4, 8, 5},      // bg_base: CRT phosphor obsidian-green
        {85, 200, 110}, // border_line
        {155, 255, 175},// border_corner
        {6, 14, 9},     // border_bg
        {140, 255, 165},// hud_primary
        {185, 255, 205},// hud_title
        render_level_moonlit_valley,
    },
    {
        LEVEL_DEEP_IN_THE_WATER,
        "DEEP IN THE WATER",
        {4, 9, 18},     // bg_base: Deep abyssal midnight navy
        {55, 175, 235}, // border_line: Ocean cyan-blue
        {180, 245, 255},// border_corner: Ice-cyan highlight
        {6, 14, 28},    // border_bg
        {120, 230, 255},// hud_primary: Luminous aqua
        {205, 250, 255},// hud_title: Crisp ice-white cyan
        render_level_deep_in_the_water,
    },
    {
        LEVEL_VICE,
        "MEDIEVAL VICE",
        {5, 7, 12},      // bg_base: Obsidian void
        {115, 180, 235}, // border_line: Forged steel-blue
        {255, 205, 85},  // border_corner: Torchlight gold
        {10, 14, 22},    // border_bg
        {180, 220, 255}, // hud_primary: Silver-blue steel
        {255, 222, 115}, // hud_title: Royal torch gold
        render_level_vice,
    },
};

static void draw_border_and_hud(const GameState *state,
                                const LevelDef *lvl_def) {
  int w = state->term_width;
  int h = state->term_height;

  ColorRGB border_line = lvl_def->border_line;
  ColorRGB border_corner = lvl_def->border_corner;
  ColorRGB border_bg = lvl_def->border_bg;

  int left_x = 1;
  int right_x = w - 2;
  int top_y = 0;
  int bot_y = h - 1;

  for (int x = left_x + 1; x < right_x; x++) {
    set_cell_ascii(top_y, x, '-', border_line, border_bg);
    set_cell_ascii(bot_y, x, '-', border_line, border_bg);
  }
  for (int y = top_y + 1; y < bot_y; y++) {
    set_cell_ascii(y, left_x, '|', border_line, border_bg);
    set_cell_ascii(y, right_x, '|', border_line, border_bg);
  }

  set_cell_ascii(top_y, left_x, '+', border_corner, border_bg);
  set_cell_ascii(top_y, right_x, '+', border_corner, border_bg);
  set_cell_ascii(bot_y, left_x, '+', border_corner, border_bg);
  set_cell_ascii(bot_y, right_x, '+', border_corner, border_bg);

  ColorRGB hud_primary = lvl_def->hud_primary;
  ColorRGB hud_title = lvl_def->hud_title;

  unsigned long score = state->tick / 3UL;
  unsigned long hi_score = score > 12450UL ? score : 12450UL;

  char buf[64];
  snprintf(buf, sizeof(buf), "SCORE: %06lu", score);
  draw_string_fg(1, 3, buf, hud_primary, w);

  snprintf(buf, sizeof(buf), "HI-SCORE: %06lu", hi_score);
  draw_string_fg(2, 3, buf, hud_primary, w);

  const char *title = "CSCAPE v1.0";
  draw_string_fg(1, (w - (int)strlen(title)) / 2, title, hud_title, w);

  snprintf(buf, sizeof(buf), "[ENTER] LVL %d: %s", (int)lvl_def->id + 1,
           lvl_def->name);
  draw_string_fg(2, (w - (int)strlen(buf)) / 2, buf, hud_primary, w);

  snprintf(buf, sizeof(buf), "SPEED: %s", state->is_paused ? "||" : ">>");
  draw_string_fg(1, w - 13, buf, hud_primary, w);

  const char *state_label;
  if (state->is_paused) {
    state_label = "PAUSED ";
  } else if (lvl_def->id == LEVEL_DEEP_IN_THE_WATER) {
    int c_tick = (int)(state->tick % 96UL);
    if (state->fish_ticks > 0) {
      state_label = "FISH!  ";
    } else {
      state_label = (c_tick >= 52 && c_tick < 76) ? "SPLASH " : "DROPLET";
    }
  } else if (lvl_def->id == LEVEL_VICE) {
    state_label = (state->jump_ticks > 0) ? "TORCH! " : "ON WATCH";
  } else {
    state_label = (state->jump_ticks > 0) ? "JUMPING" : "LOOKING";
  }
  snprintf(buf, sizeof(buf), "STATE: %s", state_label);
  draw_string_fg(2, w - 18, buf, hud_primary, w);
}

void render_frame(GameState *state) {
  state->term_width = tb_width();
  state->term_height = tb_height();

  // 5 valley ground strata rows (ground_y .. ground_y + 4) + 1 bottom border row
  state->ground_y = state->term_height - 6;

  LevelId lvl = (state->current_level < LEVEL_COUNT) ? state->current_level
                                                     : LEVEL_MOONLIT_VALLEY;
  const LevelDef *lvl_def = &LEVEL_TABLE[lvl];

  tb_set_clear_attrs(rgb_to_tb(lvl_def->bg_base), rgb_to_tb(lvl_def->bg_base));
  tb_clear();

  if (state->term_width < 50 || state->term_height < 22) {
    tb_print(2, 1, rgb_to_tb(lvl_def->hud_primary), rgb_to_tb(lvl_def->bg_base),
             "cscape: Enlarge terminal window (min 50x22)");
    tb_present();
    return;
  }

  lvl_def->render(state);
  draw_border_and_hud(state, lvl_def);

  tb_present();
}

void render_cleanup(void) {
  tb_shutdown();
}
