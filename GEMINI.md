# Project Context & Workspace Rules: `cscape`

`cscape` is a zero-allocation, 60 FPS retro terminal interactive soundscape/landscape and procedural ASCII/Braille shader showcase written in **C11** for **macOS**. It renders 24-bit TrueColor visuals using `termbox2` and streams background audio via `miniaudio`.

---

## 1. Tech Stack & Build Configuration
- **Standard & Compiler:** C11 compiled with Apple `clang`.
- **Compiler Flags:** `-std=c11 -Wall -Wextra -Werror -Iinclude -O2` (warnings are treated as hard errors).
- **Linker Flags:** `-lm -lpthread` (math library for procedural shaders; POSIX threads for `miniaudio`).
- **Rendering Backend:** `include/termbox.h` (`termbox2` configured with `#define TB_OPT_ATTR_W 32` for 24-bit `TB_OUTPUT_TRUECOLOR` and UTF-8 Braille sub-pixel rendering `U+2800..U+28FF` enabled via `setlocale(LC_ALL, "")`).
- **Audio Backend:** `include/miniaudio.h` (`miniaudio` single-header library streaming from `assets/`).

---

## 2. Repository Structure
- `Makefile`: Build targets (`make all`, `make run`, `make clean`) producing the `./cscape` binary.
- `include/game.h`: Core constants (`TARGET_FPS = 60`, `FRAME_TIME_NS = 16666667L`, jump/fish timers), `LevelId` enum, `ColorRGB` and `GameState` structs, and public `render_*` / `audio_*` API declarations.
- `include/sprites.h`: Compile-time `static const` ASCII sprite tables stored in `.rodata`:
  - **Level 1:** `MOON_SPRITE` (38x22), `CLIFF_BOY_FRAMES` (2 frames), `DOG_RUN_FRAMES` (4 frames), `DOG_JUMP_FRAMES` (3 frames), `PROP_SPRITES` (3 valley prop types).
  - **Level 2:** `WATER_DROP_FRAMES` (3 frames), `WATER_SPLASH_FRAMES` (4 frames), `WATER_FLORA_SPRITES` (3 types), `FISH_FRAMES` (3 frames), `FISH_SPLASH_FRAMES` (3 frames).
  - **Level 3:** `VICE_COP_SPRITE` (56x28 ASCII material map).
- `include/termbox.h` & `include/miniaudio.h`: Vendored single-header libraries. **Do not modify directly.**
- `src/main.c`: Entry point, POSIX `sigaction` (`SIGINT`) handler, stack-allocated `GameState`, non-blocking `tb_peek_event` input loop, and `nanosleep` 60 Hz frame pacing.
- `src/render.c`: `TB_IMPL` instantiation, color/noise math helpers, Braille UTF-8 encoder, per-level procedural renderers dispatched via `LEVEL_TABLE`, and bordered viewport/HUD drawing (`draw_border_and_hud`).
- `src/audio.c`: `MINIAUDIO_IMPLEMENTATION` instantiation, hybrid audio engine in `.bss` combining per-level custom file streaming (`assets/valley.*`, `assets/water.*`, `assets/vice.*`, `assets/level1..3.*`, `assets/music.*`) with a real-time 48 kHz stereo procedural synthesizer & ping-pong delay (`ProceduralSynth`), level-switching (`audio_set_level`), action SFX (`audio_trigger_action`), pause/mute controls, and teardown.
- `assets/`: Optional drop-in directory for per-level or global background music files (falls back automatically to built-in procedural level music when empty).
- `references/`: Visual reference artwork for scenes and palettes.

---

## 3. Levels, Controls & HUD Specification

### Interactive Controls (`src/main.c`)
| Keybinding | Action |
| :--- | :--- |
| `q` / `Q` / `ESC` / `Ctrl+C` | Cleanly exit game (restores terminal & uninitializes audio) |
| `Enter` | Cycle through levels (`LEVEL_MOONLIT_VALLEY` → `LEVEL_DEEP_IN_THE_WATER` → `LEVEL_VICE`) |
| `Space` / `w` / `W` / `Up` | Trigger level action (unpauses if paused): Dog jump (Lvl 1), Koi fish leap (Lvl 2), Siren/Lens flash (Lvl 3) |
| `p` / `P` | Toggle pause (`SPEED: ||`, pauses tick counter and background music) |
| `m` / `M` | Toggle audio mute (`ma_engine_set_volume`) |

### Multi-Level Dispatch (`LEVEL_TABLE` in `src/render.c`)
1. **Level 1 — `MOONLIT VALLEY` (`LEVEL_MOONLIT_VALLEY`):**
   - **Palette:** Retro phosphor-green CRT (`BG_BASE {4, 8, 5}`).
   - **Scene:** 3-layer parallax twinkling stars, striated background/foreground cloud banks, procedural 3D cratered moon shader (`draw_planet_shader`) with dark maria basins, Tycho crater, and Braille corona, left craggy cliff with animated watcher, scrolling valley props & 5-row stratified ground, and a 4-frame running / 3-frame parabolic jumping dog kicking up Braille dust.
   - **HUD State:** `LOOKING` (default) | `JUMPING` (when `jump_ticks > 0`).
2. **Level 2 — `DEEP IN THE WATER` (`LEVEL_DEEP_IN_THE_WATER`):**
   - **Palette:** Abyssal midnight-navy (`{4, 9, 18}`) with bioluminescent aqua, cyan, emerald, and coral-pink accents.
   - **Scene:** Cavern ceiling with stalactites and drifting mist, 96-tick (1.6s) repeating water droplet fall & crown splash cycle, expanding concentric surface ripples, swaying underwater flora & rising Braille micro-bubbles, plus an on-demand parabolic leaping koi fish (`FishPath`).
   - **HUD State:** `DROPLET` | `SPLASH` (ticks 52–75) | `FISH!` (when `fish_ticks > 0`).
3. **Level 3 — `MEDIEVAL VICE` (`LEVEL_VICE`):**
   - **Palette:** Obsidian void (`{5, 7, 12}`), forged steel-blue (`{115, 190, 250}`), and torchlight amber-gold (`{255, 185, 52}`).
   - **Scene:** Full-screen procedural knight-enforcer portrait mapped from `VICE_COP_SPRITE` (forged sallet helmet with central ridge & rivets, glowing royal shield badge, hinged cheek guards, mirrored 80s aviator sunglasses reflecting torchlight glints, handlebar mustache, ASCII chainmail gorget, and steel shoulder pauldrons with a dual-tone blue/gold silhouette aura).
   - **HUD State:** `ON WATCH` (default) | `TORCH!` (when `jump_ticks > 0`).

---

## 4. Strict Architectural Invariants
1. **Zero Heap Allocation in the Game Loop:**
   - **NEVER** call `malloc()`, `calloc()`, `realloc()`, or `free()` inside `render_frame()` or the `while (state.is_running)` loop.
   - All per-frame working data must live on the stack (fixed-size arrays, value structs) or in `static`/`const` storage (`.bss`/`.rodata`).
2. **Dynamic Terminal Bounds & Minimum Viewport Guard:**
   - Query `tb_width()` and `tb_height()` at the start of every `render_frame()`.
   - Never hardcode terminal dimensions; compute positions proportionally and clamp with `clampi()`/`clampf()`.
   - Preserve the minimum viewport guard (`term_width < 50 || term_height < 22`), which displays a resize prompt instead of rendering out-of-bounds cells.
3. **Clean Exit & Resource Safety Guarantee:**
   - `SIGINT` (`Ctrl+C`) and quit keys must always exit the loop cleanly so `audio_cleanup(&state)` and `render_cleanup()` (`tb_shutdown()`) execute before `main` returns, preventing corrupted macOS terminal state.
4. **Single-Header Implementation Isolation:**
   - `TB_IMPL` must only be defined in `src/render.c`, and `MINIAUDIO_IMPLEMENTATION` must only be defined in `src/audio.c`, wrapped in `#pragma clang diagnostic` push/pop blocks so third-party headers do not trigger `-Werror` failures.

---

## 5. Agent Execution & Verification Workflow
When modifying or extending this repository, follow the **Explore → Plan → Execute → Verify** protocol:
1. **Explore & Plan:** Check `include/game.h`, `include/sprites.h`, and `src/render.c` before proposing changes. Keep changes modular and consistent with `LEVEL_TABLE` and existing helper functions (`set_cell_ascii`, `set_cell_fg`, `set_cell_utf8`, `lerp_rgb`, `value_noise2d`).
2. **Compile & Verify:** After every code edit, run:
   ```sh
   make clean && make all
   ```
   Ensure compilation succeeds with **zero warnings and zero errors** under `-std=c11 -Wall -Wextra -Werror`.
3. **Interactive TTY Note:** Do **not** run `./cscape` or `make run` unattended in a non-interactive background shell, as `termbox2` requires an interactive controlling TTY (`/dev/tty`) and will block waiting for input until terminated.
4. **Safety Guardrails:**
   - Restrict all file edits strictly to the workspace directory.
   - Do not modify `include/termbox.h` or `include/miniaudio.h`.
   - Never run destructive git or filesystem commands (`git reset --hard`, `rm -rf`) without explicit user confirmation.
