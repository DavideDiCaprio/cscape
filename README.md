# cscape

`cscape` is a zero-allocation, 60 FPS retro terminal interactive soundscape and procedural ASCII/Braille shader showcase written in **C11** for **macOS**. It renders 24-bit TrueColor visuals using `termbox2` and streams real-time procedural synth & background audio via `miniaudio`.

## Features

- **Zero Heap Allocation in the Render Loop:** All per-frame state lives on the stack or in `.bss`/`.rodata`.
- **24-bit TrueColor & Sub-pixel Braille Shaders:** Uses UTF-8 Braille patterns (`U+2800..U+28FF`) and smooth RGB interpolation.
- **Hybrid Procedural Audio Engine:** Built-in 48 kHz stereo procedural synthesizer with ping-pong delay, plus automatic support for drop-in custom audio tracks in `assets/`.
- **3 Interactive Levels:**
  1. **Moonlit Valley:** Retro phosphor-green CRT aesthetic with 3-layer parallax stars, procedural 3D cratered moon shader, cliffside watcher, and a running/jumping dog.
  2. **Deep in the Water:** Abyssal midnight-navy cavern with bioluminescent flora, falling water droplet & crown splash cycles, concentric surface ripples, and a leaping koi fish.
  3. **Medieval Vice:** Full-screen procedural knight-enforcer portrait with torchlight reflections, mirrored 80s aviator sunglasses, and a dual-tone silhouette aura.

## Requirements & Build

Requires macOS with Apple `clang` and a TrueColor-capable terminal (e.g., iTerm2, Ghostty, Kitty, WezTerm, or macOS Terminal).

```sh
# Build the binary
make all

# Build and launch
make run

# Remove compiled binary
make clean
```

## Controls

| Key | Action |
| :--- | :--- |
| `Enter` | Cycle through levels (`Moonlit Valley` → `Deep in the Water` → `Medieval Vice`) |
| `Space` / `W` / `Up` | Trigger level action (Dog jump / Koi fish leap / Torch & siren flash) |
| `P` | Toggle pause |
| `M` | Toggle audio mute |
| `Q` / `ESC` / `Ctrl+C` | Cleanly exit and restore terminal state |

## Custom Audio (Optional)

Drop `.mp3`, `.wav`, or `.flac` files into `assets/` (`assets/valley.*`, `assets/water.*`, `assets/vice.*`, `assets/level1..3.*`, or `assets/music.*`) to override the built-in procedural synthesizer tracks.
